// test_scene_align.jsx — Align 3D 검증 씬 (Align_Test 1920x1080)
//   A 3D 200x200 @ (300,300,0)
//   B 3D 200x200 @ (900,600,200)  scale 50%  → 월드 폭 100
//   C 3D 200x200 @ (1500,800,-300) 부모 = ParentNull(3D, pos 0,0,0, Z회전 45°) → 부모 회전 하에서도 정확히 이동해야 함
//   D 2D 200x200 @ (600,900)
//   (카메라 없음)
(function () {
    app.beginUndoGroup("Align test scene");
    var comp = app.project.items.addComp("Align_Test", 1920, 1080, 1, 5, 30);
    comp.openInViewer();
    function solid(name, x, y, z, col) {
        var l = comp.layers.addSolid(col, name, 200, 200, 1);
        l.threeDLayer = true;
        l.property("Position").setValue([x, y, z]);
        return l;
    }
    var pn = comp.layers.addNull(); pn.name = "ParentNull"; pn.threeDLayer = true;
    pn.property("Position").setValue([0, 0, 0]);
    pn.property("Z Rotation").setValue(45);
    var a = solid("A", 300, 300, 0, [1, 0.3, 0.3]);
    var b = solid("B", 900, 600, 200, [0.3, 1, 0.3]); b.property("Scale").setValue([50, 50, 50]);
    var c = solid("C", 1500, 800, -300, [0.3, 0.5, 1]);
    c.parent = pn;   // parent 설정 시 AE 가 월드 위치를 유지하도록 Position 을 재계산함
    var d = comp.layers.addSolid([1, 1, 0.3], "D", 200, 200, 1);
    d.property("Position").setValue([600, 900]);
    for (var i = 1; i <= comp.numLayers; i++) comp.layer(i).selected = false;
    a.selected = true; b.selected = true; c.selected = true; d.selected = true;
    app.endUndoGroup();
    return "ready: 4 layers selected (3x3D + 1x2D)";
})();
