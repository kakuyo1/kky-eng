#!/usr/bin/env node

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

function findResponseFile() {
    const requested = option("--response");
    if (requested) return path.resolve(root, requested);

    const build = option("--build-dir");
    const candidates = build
        ? [path.resolve(root, build)]
        : [path.resolve(root, "build-ninja"), path.resolve(root, "build")];
    for (const candidate of candidates) {
        const responseDir = path.join(candidate, "src", "app", ".rcc", "qmllint");
        if (fs.existsSync(responseDir)) {
            const response = fs.readdirSync(responseDir)
                .filter(file => file.endsWith(".rsp"))
                .sort()[0];
            if (response) return path.join(responseDir, response);
        }
    }
    throw new Error("qml-ast-metrics: no configured QML response file found");
}

function readQmlFiles(response) {
    return fs.readFileSync(response, "utf8")
        .split(/\r?\n/)
        .map(line => line.trim())
        .filter(line => line.endsWith(".qml"))
        .map(file => path.isAbsolute(file) ? file : path.resolve(root, file));
}

function children(node) {
    return node.namedChildren || [];
}

function descendantCount(node, types) {
    let count = 0;
    if (types.has(node.type)) count++;
    for (const child of children(node)) count += descendantCount(child, types);
    return count;
}

function walk(node, visit) {
    visit(node);
    for (const child of children(node)) walk(child, visit);
}

function countLogicalOperators(node) {
    let count = 0;
    walk(node, current => {
        if (current.type === "binary_expression" && /&&|\|\|/.test(current.text)) count++;
    });
    return count;
}

function decisionCount(node) {
    return descendantCount(node, new Set([
        "if_statement",
        "for_in_statement",
        "for_statement",
        "while_statement",
        "do_statement",
        "switch_statement",
        "catch_clause",
        "ternary_expression",
    ]));
}

function decisionNesting(node, depth = 0) {
    let maximum = depth;
    const decisions = new Set([
        "if_statement",
        "for_in_statement",
        "for_statement",
        "while_statement",
        "do_statement",
        "switch_statement",
        "catch_clause",
        "ternary_expression",
    ]);
    for (const child of children(node)) {
        const nextDepth = decisions.has(child.type) ? depth + 1 : depth;
        maximum = Math.max(maximum, decisionNesting(child, nextDepth));
    }
    return maximum;
}

function functionMetric(node, kind, sourceLines) {
    const name = node.childForFieldName("name")?.text || "<handler>";
    const body = node.type === "function_declaration"
        ? node.childForFieldName("body")
        : node.childForFieldName("value");
    const parameters = node.type === "function_declaration"
        ? node.childForFieldName("parameters")
        : undefined;
    const parameterCount = parameters
        ? parameters.namedChildren.filter(child => child.type.endsWith("parameter")).length
        : 0;
    const startLine = node.startPosition.row + 1;
    const endLine = node.endPosition.row + 1;
    const bodyNode = body || node;
    const decisions = decisionCount(bodyNode);
    const logicalOperators = countLogicalOperators(bodyNode);
    const complexity = 1 + decisions + logicalOperators;
    const cognitive = decisions + logicalOperators + Math.max(0, decisionNesting(bodyNode) - 1);
    const bodyLines = sourceLines.slice(bodyNode.startPosition.row, bodyNode.endPosition.row + 1);
    const codeLines = bodyLines.filter(line => line.trim() !== "" && !line.trim().startsWith("//"));

    return {
        Name: name,
        Kind: kind,
        StartLine: startLine,
        EndLine: endLine,
        PhysicalLoc: endLine - startLine + 1,
        CodeLoc: codeLines.length,
        Parameters: parameterCount,
        Cyclomatic: complexity,
        Cognitive: cognitive,
        DecisionNesting: decisionNesting(bodyNode),
        Decisions: decisions,
        LogicalOperators: logicalOperators,
    };
}

function analyzeFile(file) {
    const source = fs.readFileSync(file, "utf8");
    const sourceLines = source.split(/\r?\n/);
    const parser = new Parser();
    parser.setLanguage(Qml);
    const tree = parser.parse(source);
    const objects = [];
    const functions = [];
    const bindingRoots = new Set();
    let bindings = 0;
    let handlers = 0;
    let maxObjectDepth = 0;
    let errorNodes = 0;
    let missingNodes = 0;
    const bindingEdges = [];
    let maxBindingFanOut = 0;

    function visit(node, objectDepth = 0) {
        if (node.type === "ERROR") errorNodes++;
        if (node.type === "MISSING") missingNodes++;

        let nextObjectDepth = objectDepth;
        if (node.type === "ui_object_definition") {
            nextObjectDepth++;
            maxObjectDepth = Math.max(maxObjectDepth, nextObjectDepth);
            objects.push({
                Type: node.childForFieldName("type_name")?.text || "<unknown>",
                Line: node.startPosition.row + 1,
                Depth: nextObjectDepth,
            });
        }

        if (node.type === "ui_binding") {
            const name = node.childForFieldName("name")?.text || "";
            const value = node.childForFieldName("value");
            if (name.startsWith("on") && /^on[A-Z]/.test(name)) {
                handlers++;
                if (value?.type === "statement_block") {
                    functions.push(functionMetric(node, "signal-handler", sourceLines));
                }
            } else if (name !== "id") {
                bindings++;
                if (value) {
                    const roots = new Set();
                    walk(value, child => {
                        if (child.type === "member_expression") {
                            const object = child.childForFieldName("object");
                            if (object?.type === "identifier") {
                                bindingRoots.add(object.text);
                                roots.add(object.text);
                            }
                        }
                    });
                    maxBindingFanOut = Math.max(maxBindingFanOut, roots.size);
                    for (const root of roots) bindingEdges.push({ Target: name, Root: root });
                }
            }
        }

        if (node.type === "function_declaration") {
            functions.push(functionMetric(node, "function", sourceLines));
        }

        for (const child of children(node)) visit(child, nextObjectDepth);
    }
    visit(tree.rootNode);

    const relative = path.relative(root, file).replaceAll(path.sep, "/");
    return {
        File: relative,
        ParseErrors: errorNodes,
        MissingNodes: missingNodes,
        QmlObjects: objects.length,
        MaxObjectDepth: maxObjectDepth,
        PropertyBindings: bindings,
        SignalHandlers: handlers,
        BindingMemberRoots: [...bindingRoots].sort(),
        BindingMemberRootCount: bindingRoots.size,
        BindingEdges: bindingEdges,
        BindingEdgeCount: bindingEdges.length,
        MaxBindingFanOut: maxBindingFanOut,
        Functions: functions,
        Objects: objects,
    };
}

const response = findResponseFile();
const files = readQmlFiles(response);
if (files.length === 0) throw new Error("qml-ast-metrics: response file contains no QML files");

const metrics = files.map(analyzeFile);
const componentNames = new Set(files
    .filter(file => file.includes(`${path.sep}components${path.sep}`) || file.includes("/components/"))
    .map(file => path.basename(file, ".qml")));
const componentFanIn = {};
for (const file of metrics) {
    const references = {};
    for (const object of file.Objects) {
        if (!componentNames.has(object.Type)) continue;
        references[object.Type] = (references[object.Type] || 0) + 1;
        componentFanIn[object.Type] = (componentFanIn[object.Type] || 0) + 1;
    }
    file.ComponentReferences = references;
}
const functions = metrics.flatMap(file => file.Functions.map(fn => ({ ...fn, File: file.File })));
const summary = {
    FileCount: metrics.length,
    ParseErrors: metrics.reduce((sum, file) => sum + file.ParseErrors, 0),
    MissingNodes: metrics.reduce((sum, file) => sum + file.MissingNodes, 0),
    QmlObjects: metrics.reduce((sum, file) => sum + file.QmlObjects, 0),
    MaxObjectDepth: Math.max(...metrics.map(file => file.MaxObjectDepth)),
    PropertyBindings: metrics.reduce((sum, file) => sum + file.PropertyBindings, 0),
    SignalHandlers: metrics.reduce((sum, file) => sum + file.SignalHandlers, 0),
    BindingMemberRootCount: metrics.reduce((sum, file) => sum + file.BindingMemberRootCount, 0),
    FunctionCount: functions.filter(fn => fn.Kind === "function").length,
    SignalHandlerFunctionCount: functions.filter(fn => fn.Kind === "signal-handler").length,
    BindingEdgeCount: metrics.reduce((sum, file) => sum + file.BindingEdgeCount, 0),
    MaxBindingFanOut: Math.max(0, ...metrics.map(file => file.MaxBindingFanOut)),
    MaxCyclomatic: Math.max(0, ...functions.map(fn => fn.Cyclomatic)),
    MaxCognitive: Math.max(0, ...functions.map(fn => fn.Cognitive)),
    FunctionsCyclomaticOver10: functions.filter(fn => fn.Cyclomatic > 10).length,
    FunctionsCognitiveOver15: functions.filter(fn => fn.Cognitive > 15).length,
    FunctionsPhysicalLocOver50: functions.filter(fn => fn.PhysicalLoc > 50).length,
    ComponentFanIn: componentFanIn,
};

const result = {
    GeneratedAt: new Date().toISOString(),
    Tool: {
        Name: "qml-ast-metrics.js",
        Parser: "tree-sitter-qmljs 0.3.1",
    },
    Scope: { ResponseFile: response, Files: metrics },
    Summary: summary,
    HotFunctions: functions
        .filter(fn => fn.Cyclomatic > 10 || fn.Cognitive > 15 || fn.PhysicalLoc > 50)
        .sort((a, b) => b.Cyclomatic - a.Cyclomatic || b.Cognitive - a.Cognitive),
};

const output = JSON.stringify(result, null, 2);
const outputPath = option("--output");
if (outputPath) {
    fs.writeFileSync(path.resolve(root, outputPath), `${output}\n`, "utf8");
    console.log(`qml-ast-metrics: wrote ${path.resolve(root, outputPath)}`);
} else {
    console.log(output);
}
