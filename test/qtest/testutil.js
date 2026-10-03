// Reaching into a component from a case.
//
// The components name no ids outward, so a case has to walk to what it wants. Two functions do
// all of it; the rest is a case saying which text or which rectangle it means.

/// Every object declared under `root`, depth first, that `predicate` accepts.
///
/// `data`, not `children`: a component declares things that are not items -- a pointer handler,
/// a Timer, the list Window a DropdownField opens -- and `children` is items only. The list
/// Window is exactly where that component's rows live, so stopping at the visual tree would
/// reach the button and nothing else.
function findAll(root, predicate) {
    const found = [];
    (function collect(object) {
        const data = object.data;
        if (!data)
            return;
        for (let i = 0; i < data.length; ++i) {
            if (predicate(data[i]))
                found.push(data[i]);
            collect(data[i]);
        }
    })(root);
    return found;
}

/// The Text items under `root`, in declaration order.
function textsUnder(root) {
    return findAll(root, function (o) { return typeof o.text === "string"; });
}

/// The texts under `root` that live in a window of their own rather than in `root`'s.
///
/// The DropdownField keeps its list in one, and its button's label in the surface's window.
/// Filters by the window an item ends up in, not by where it was declared, so a nested Window
/// is all this has to know about.
function textsInNestedWindow(root) {
    return textsUnder(root).filter(function (t) {
        return t.Window.window !== root.Window.window;
    });
}

/// @return The first object under `root` whose QML type is `typeName`, or null.
///
/// A surface declares no objectName and none of Main.qml's ids reach outside it, so its own
/// type name is the only handle there is on one of them. QML spells that as
/// `StatsPopup_QMLTYPE_7`, which is what the prefix match is for.
function ofType(root, typeName) {
    return findAll(root, function (o) {
        return o.toString().indexOf(typeName + "_QMLTYPE") === 0;
    })[0];
}

/// @return The first of `texts` whose content is one of `candidates`, or null.
///
/// The row a case wants is the one showing a label it already knows; reading the delegate by
/// position would make the case depend on the order the engine happened to create them in.
function textWith(texts, candidates) {
    for (let i = 0; i < texts.length; ++i) {
        if (candidates.indexOf(texts[i].text) >= 0)
            return texts[i];
    }
    return null;
}
