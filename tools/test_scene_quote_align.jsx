// test_scene_quote_align.jsx — Quote Align 검증용 테스트 씬 생성
// 실행: File > Scripts > Run Script File... (AE 메인 엔진)
// 만드는 것: 1920x1080 컴프 "QA_Test" + 레이어 4개
//   1) 포인트 텍스트, 여는 따옴표로 시작, 2줄  → [BOX] 변환 대상
//   2) 박스 텍스트, 여는 따옴표로 시작        → hangingRoman 세트 대상
//   3) 포인트 텍스트, 소스텍스트 키프레임 있음 → skip 대상
//   4) 솔리드                                  → skip 대상 (텍스트 아님)
// 모두 선택된 상태로 끝남 → 패널의 Quote Align 버튼을 누르면 됨.
(function () {
    app.beginUndoGroup("QA test scene");
    var comp = app.project.items.addComp("QA_Test", 1920, 1080, 1, 5, 30);
    comp.openInViewer();

    // 4) 솔리드 (배경)
    var solid = comp.layers.addSolid([0.12, 0.12, 0.12], "BG_Solid", 1920, 1080, 1);

    function styleDoc(doc, size) {
        doc.fontSize = size;
        doc.applyFill = true;
        doc.fillColor = [1, 1, 1];
        doc.applyStroke = false;
        doc.justification = ParagraphJustification.LEFT_JUSTIFY;
        return doc;
    }

    // 1) 포인트 텍스트 2줄
    var t1 = comp.layers.addText("“인용 자막 테스트입니다\r둘째 줄 본문입니다”");
    t1.name = "PointText_Quote";
    var p1 = t1.property("Source Text");
    p1.setValue(styleDoc(p1.value, 72));
    t1.property("Position").setValue([200, 300]);

    // 2) 박스 텍스트
    var t2 = comp.layers.addBoxText([1200, 300], "“박스 텍스트 인용문입니다\r두 번째 줄입니다”");
    t2.name = "BoxText_Quote";
    var p2 = t2.property("Source Text");
    p2.setValue(styleDoc(p2.value, 72));
    t2.property("Position").setValue([200, 600]);

    // 3) 소스텍스트 키프레임 있는 포인트 텍스트
    var t3 = comp.layers.addText("“키프레임 텍스트”");
    t3.name = "PointText_Keyed";
    var p3 = t3.property("Source Text");
    p3.setValue(styleDoc(p3.value, 60));
    p3.setValueAtTime(0, p3.value);
    var d3 = p3.value; d3.text = "“변경된 텍스트”";
    p3.setValueAtTime(2, d3);
    t3.property("Position").setValue([200, 950]);

    for (var i = 1; i <= comp.numLayers; i++) comp.layer(i).selected = true;
    app.endUndoGroup();
    alert("QA_Test scene ready: 4 layers selected.\nNow click Quote Align in BANG_Toolbox.");
})();
