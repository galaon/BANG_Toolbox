// test_scene_null.jsx — Green Null 위치/순서 검증 씬
// 컴프 "Null_Test" 1920x1080:
//   1) Top_Solid      (선택 안 함, 최상단)          — Null 이 이 위로 올라가면 안 됨
//   2) A_Solid  200x200 @ (400,300)   선택         — 오브젝트 중심 = (400,300)
//   3) B_Masked 600x600 @ (960,540)   선택, 마스크 (우하단 사분면: 로컬 300..600) — 마스크 중심 = (1110,690)
//   4) C_Solid  200x200 @ (1500,800)  선택, scale 50% — 오브젝트 중심 = (1500,800)
//   5) Bottom_Solid (선택 안 함)
// 기대: Null index = 2 (A_Solid 바로 위), Position = 평균((400,300),(1110,690),(1500,800)) = (1003.33, 596.67)
(function () {
    app.beginUndoGroup("Null test scene");
    var comp = app.project.items.addComp("Null_Test", 1920, 1080, 1, 5, 30);
    comp.openInViewer();
    function solid(name, w, h, x, y, col) {
        var l = comp.layers.addSolid(col, name, w, h, 1);
        l.property("Position").setValue([x, y]);
        return l;
    }
    var bottom = solid("Bottom_Solid", 1920, 1080, 960, 540, [0.1, 0.1, 0.1]);
    var c = solid("C_Solid", 200, 200, 1500, 800, [0.2, 0.6, 1]);
    c.property("Scale").setValue([50, 50]);
    var b = solid("B_Masked", 600, 600, 960, 540, [1, 0.5, 0.2]);
    var m = b.property("ADBE Mask Parade").addProperty("ADBE Mask Atom");
    var sh = new Shape();
    sh.vertices = [[300, 300], [600, 300], [600, 600], [300, 600]];
    sh.closed = true;
    m.property("ADBE Mask Shape").setValue(sh);
    var a = solid("A_Solid", 200, 200, 400, 300, [0.4, 1, 0.4]);
    var top = solid("Top_Solid", 100, 100, 100, 100, [1, 1, 1]);
    for (var i = 1; i <= comp.numLayers; i++) comp.layer(i).selected = false;
    a.selected = true; b.selected = true; c.selected = true;
    app.endUndoGroup();
    return "ready: " + comp.numLayers + " layers, 3 selected";
})();
