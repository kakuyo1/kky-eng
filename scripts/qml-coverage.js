#!/usr/bin/env node

// Which parts of the QML module a test run actually reached.
//
// Qt has no line-coverage tool for QML, and the reason is structural rather than an omission:
// a .qml file mostly declares objects, and declaring an object is not a line that "executes".
// What does execute is the JavaScript in it -- property bindings, signal handlers and
// functions -- and the one place Qt reports that is the QML profiler. qmlprofiler records a
// Binding / HandlingSignal / Javascript event for every expression it evaluates, each carrying
// the file and line it came from, so the union of those locations is the executed set.
//
// The denominator comes from the same parse tree the rest of the metrics use
// (tree-sitter-qmljs): every binding, signal handler and function in the module. Bindings whose
// value is a bare literal are left out of both sides -- the QML compiler folds them into the
// object's creation, so the engine never evaluates them and no profiler event can ever name
// them. Counting them would be a permanent, unwinnable deduction.
//
// This is *location* coverage, not line coverage: it says whether an expression at a line ran,
// not whether every line of a multi-line expression ran. It is not comparable to the C++
// numbers OpenCppCoverage produces, and the two are never averaged together.
//
// Usage: node scripts/qml-coverage.js --trace <trace.qtd> [--trace <more.qtd>...]
//          [--qml-root src/app/qml] [--json <file>]

const fs = require("fs");
const path = require("path");
const Parser = require("tree-sitter");
const Qml = require("tree-sitter-qmljs");

const root = process.cwd();
const args = process.argv.slice(2);

function option(name) {
    const index = args.indexOf(name);
    return index === -1 ? undefined : args[index + 1];
}

function optionAll(name) {
    const found = [];
    for (let index = 0; index < args.length - 1; index++) {
        if (args[index] === name) found.push(args[index + 1]);
    }
    return found;
}

/// A profiler event names the file it ran from by its qrc path. The module is registered as
/// `Lens` (src/app/CMakeLists.txt) and Qt's default resource layout puts its files under
/// `/qt/qml/<URI>/`, so the prefix is that plus the `qml/` the sources sit in.
const RESOURCE_PREFIX = "qrc:/qt/qml/Lens/qml/";

/// How a binding whose value can never produce a profiler event is recognised. A literal is
/// folded into the object's construction; the engine never evaluates an expression for it.
const LITERAL_TYPES = new Set(["number", "string", "true", "false", "null", "undefined"]);

function qmlFiles(dir) {
    const found = [];
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name);
        if (entry.isDirectory()) found.push(...qmlFiles(full));
        else if (entry.name.endsWith(".qml")) found.push(full);
    }
    return found.sort();
}

/// The expression a binding or property carries. Both wrap it in a single-statement block, so
/// the interesting node is one level down.
function expressionOf(node) {
    const value = node.childForFieldName("value");
    if (!value) return undefined;
    return value.type === "expression_statement" && value.namedChildren.length === 1
        ? value.namedChildren[0]
        : value;
}

function isLiteral(node) {
    if (!node) return true;
    if (LITERAL_TYPES.has(node.type)) return true;
    return node.type === "unary_expression" && LITERAL_TYPES.has(node.namedChildren[0]?.type);
}

/// Every location in @p file that a run could reach, as line numbers.
///
/// Two node types carry one: `ui_property` is a property *declaration* with an initialiser
/// (`readonly property color ink: dark ? ... : ...`), and `ui_binding` is an assignment in an
/// object body. Both are expressions the engine evaluates, and both fold away when the
/// initialiser is a bare literal.
function coverableLines(file) {
    const source = fs.readFileSync(file, "utf8");
    const parser = new Parser();
    parser.setLanguage(Qml);
    const tree = parser.parse(source);
    const lines = new Set();
    const skippedLiteralLines = [];

    function visit(node) {
        if (node.type === "ui_binding" || node.type === "ui_property") {
            const name = node.childForFieldName("name")?.text || "";
            const expression = expressionOf(node);
            if (name !== "id" && expression) {
                if (isLiteral(expression)) skippedLiteralLines.push(node.startPosition.row + 1);
                else lines.add(node.startPosition.row + 1);
            }
        }
        if (node.type === "function_declaration") lines.add(node.startPosition.row + 1);
        for (const child of node.namedChildren || []) visit(child);
    }
    visit(tree.rootNode);

    return { lines, skippedLiteralLines };
}

/// Every {file, line} the profiler reported an evaluated expression at, across every trace
/// given. One target's trace and another's are unioned: a location reached in either run counts.
function executedLocations(traces) {
    const executed = new Map();
    const counts = { Binding: 0, HandlingSignal: 0, Javascript: 0, other: 0 };

    for (const trace of traces) {
        const text = fs.readFileSync(trace, "utf8");
        for (const [, body] of text.matchAll(/<event[^>]*>([\s\S]*?)<\/event>/g)) {
            const field = tag => (body.match(new RegExp(`<${tag}>([\\s\\S]*?)<\\/${tag}>`)) || [])[1];
            const kind = field("type") || "";
            if (!(kind in counts)) {
                counts.other++;
                continue;
            }
            counts[kind]++;
            const file = field("filename") || "";
            if (!file.startsWith(RESOURCE_PREFIX)) continue;
            const relative = file.slice(RESOURCE_PREFIX.length);
            if (!executed.has(relative)) executed.set(relative, new Set());
            executed.get(relative).add(Number(field("line")));
        }
    }
    return { executed, counts };
}

const qmlRoot = path.resolve(root, option("--qml-root") || "src/app/qml");
const traces = optionAll("--trace").map(trace => path.resolve(root, trace));
if (traces.length === 0) throw new Error("qml-coverage: at least one --trace is required");

const { executed, counts } = executedLocations(traces);

const rows = [];
let skippedLiterals = 0;
for (const file of qmlFiles(qmlRoot)) {
    const relative = path.relative(qmlRoot, file).split(path.sep).join("/");
    const { lines: coverable, skippedLiteralLines } = coverableLines(file);
    skippedLiterals += skippedLiteralLines.length;
    const seen = executed.get(relative) || new Set();
    const hit = [...coverable].filter(line => seen.has(line));
    rows.push({
        file: `src/app/qml/${relative}`,
        coverable: coverable.size,
        executed: hit.length,
        // The lines the profiler named that the parse tree did not call coverable. A large
        // number here means the denominator is wrong, not that the run was thorough.
        executedNotCoverable: [...seen].filter(line => !coverable.has(line)).sort((a, b) => a - b),
        missed: [...coverable].filter(line => !seen.has(line)).sort((a, b) => a - b),
    });
}

const total = rows.reduce((sum, row) => sum + row.coverable, 0);
const hit = rows.reduce((sum, row) => sum + row.executed, 0);

const relativeToRoot = file => path.relative(root, file).split(path.sep).join("/");

const result = {
    Traces: traces.map(relativeToRoot),
    QmlRoot: relativeToRoot(qmlRoot),
    Events: counts,
    LiteralBindingsExcluded: skippedLiterals,
    Summary: { Locations: total, Executed: hit, Rate: total ? hit / total : 0 },
    Files: rows,
};

const outputPath = option("--json");
if (outputPath) {
    fs.writeFileSync(path.resolve(root, outputPath), `${JSON.stringify(result, null, 2)}\n`, "utf8");
}

console.log(`qml-coverage: traces  ${result.Traces.join(", ")}`);
console.log(`qml-coverage: events  Binding ${counts.Binding}  HandlingSignal ${counts.HandlingSignal}  Javascript ${counts.Javascript}  other ${counts.other}`);
console.log(`qml-coverage: ${hit}/${total} coverable locations reached (${(result.Summary.Rate * 100).toFixed(1)}%), ${skippedLiterals} literal bindings excluded`);
for (const row of rows) {
    const rate = row.coverable ? ((row.executed / row.coverable) * 100).toFixed(0).padStart(3) : "  -";
    console.log(`  ${rate}%  ${String(row.executed).padStart(3)}/${String(row.coverable).padEnd(3)}  ${row.file}`);
}
if (outputPath) console.log(`qml-coverage: wrote ${path.resolve(root, outputPath)}`);
