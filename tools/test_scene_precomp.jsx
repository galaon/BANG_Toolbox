// test_scene_precomp.jsx — Precomp Fit 검증 씬
// PF_Inner 1920x1080:
//   Red   200x200 @ (300,300)                         → 경계 200..400 / 200..400
//   Blue  100x100 @ (1200,700), scale 200%             → 경계 1100..1300 / 600..800
//   Mover 100x100 pos 키: 0s (500,900) → 2s (1600,950) → 현재 프레임(0s) 경계 450..550 / 850..950
//                                                        전체 모드에선 1550..1650 까지 포함
// PF_Outer 1920x1080: PF_Inner 인스턴스 2개
//   InstA @ (960,540) scale 50% rot 30
//   InstB @ (400,200) scale 100%
// 기대(current 모드, pad 0): bbox = x 200..1300, y 200..950 → 1100x750, offset (-200,-200)
//   내부 레이어 Position 모두 -200,-200 이동, 인스턴스 Anchor (960,540)->(760,340)
(function () {
    app.beginUndoGroup("Precomp test scene");
    var inner = app.project.items.addComp("PF_Inner", 1920, 1080, 1, 5, 30);
    function solid(c, name, w, h, x, y, col) {
        var l = c.layers.addSolid(col, name, w, h, 1);
        l.property("Position").setValue([x, y]);
        return l;
    }
    solid(inner, "Red", 200, 200, 300, 300, [1, 0.2, 0.2]);
    var b = solid(inner, "Blue", 100, 100, 1200, 700, [0.2, 0.4, 1]);
    b.property("Scale").setValue([200, 200]);
    var m = solid(inner, "Mover", 100, 100, 500, 900, [0.2, 1, 0.4]);
    m.property("Position").setValueAtTime(0, [500, 900]);
    m.property("Position").setValueAtTime(2, [1600, 950]);

    var outer = app.project.items.addComp("PF_Outer", 1920, 1080, 1, 5, 30);
    var ia = outer.layers.add(inner); ia.name = "InstA";
    ia.property("Position").setValue([960, 540]);
    ia.property("Scale").setValue([50, 50]);
    ia.property("Rotation").setValue(30);
    var ib = outer.layers.add(inner); ib.name = "InstB";
    ib.property("Position").setValue([400, 200]);
    outer.openInViewer();
    for (var i = 1; i <= outer.numLayers; i++) outer.layer(i).selected = false;
    ia.selected = true;
    app.endUndoGroup();
    return "ready: PF_Outer active, InstA selected";
})();
