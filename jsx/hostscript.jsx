// ============================================================
//  hostscript.jsx — ExtendScript (After Effects API)
//  모든 함수는 JSON 문자열을 반환한다.
//  { success: true/false, ... } 형태
// ============================================================

// ── 유틸리티 ──────────────────────────────────────────────────

function ok(data) {
    var obj = data || {};
    obj.success = true;
    return JSON.stringify(obj);
}

function err(msg) {
    return JSON.stringify({ success: false, error: msg });
}

function buildNullName(comp) {
    // AE 기본 Null 명명 규칙("Null 1", "Null 2" ...)을 따른다.
    var base  = "Null ";
    var count = 0;
    for (var i = 1; i <= comp.numLayers; i++) {
        var n = comp.layer(i).name;
        if (n.indexOf(base) === 0) {
            var num = parseInt(n.substr(base.length), 10);
            if (!isNaN(num) && num > count) count = num;
        }
    }
    return base + (count + 1);
}

// ── Mask / Bounds 유틸리티 ────────────────────────────────────
//
//  레이어의 "기준 바운딩 박스"를 레이어 로컬 공간으로 반환한다.
//   · 마스크가 하나라도 있으면 → 마스크 영역(유효 마스크 정점들의 합집합 bbox)
//   · 마스크가 없으면          → 기존과 동일하게 sourceRectAtTime
//  마스크 정점·sourceRect·Anchor Point 는 모두 같은 레이어 좌표계라 그대로 호환된다.
//
//  주의: 베지어 핸들로 인한 곡선 돌출은 반영하지 않고 정점 기준으로 계산한다
//        (사용자가 그린 마스크 패스 정점 = 의도한 영역으로 충분).

function _maskBoundsAtTime(layer, time) {
    var maskGroup;
    try { maskGroup = layer.property("ADBE Mask Parade"); }
    catch (e) { return null; }
    if (!maskGroup || maskGroup.numProperties === 0) return null;

    var minX, minY, maxX, maxY;
    var found = false;

    for (var m = 1; m <= maskGroup.numProperties; m++) {
        var mask = maskGroup.property(m);

        // 모드가 None 인 마스크는 매트에 영향이 없으므로 제외
        try { if (mask.maskMode === MaskMode.NONE) continue; } catch (eMode) {}

        var shape;
        try { shape = mask.property("ADBE Mask Shape").valueAtTime(time, false); }
        catch (eShape) { continue; }

        var verts = shape.vertices;
        if (!verts || verts.length === 0) continue;

        for (var i = 0; i < verts.length; i++) {
            var vx = verts[i][0];
            var vy = verts[i][1];
            if (!found) { minX = maxX = vx; minY = maxY = vy; found = true; }
            else {
                if (vx < minX) minX = vx;
                if (vx > maxX) maxX = vx;
                if (vy < minY) minY = vy;
                if (vy > maxY) maxY = vy;
            }
        }
    }

    if (!found) return null;
    return { left: minX, top: minY, width: maxX - minX, height: maxY - minY };
}

function _layerBoundsAtTime(layer, time) {
    var maskRect = _maskBoundsAtTime(layer, time);
    if (maskRect !== null) return maskRect;
    return layer.sourceRectAtTime(time, false);
}

// ── Anchor Point Setter ───────────────────────────────────────
//
//  h : 0 = left  | 0.5 = center | 1 = right
//  v : 0 = top   | 0.5 = center | 1 = bottom
//
//  알고리즘:
//    1) 레이어 바운딩 박스 취득 — 마스크가 있으면 마스크 영역, 없으면 sourceRectAtTime
//    2) 새 Anchor Point 계산
//    3) 시각적 위치 유지를 위해 Position 자동 보정
//       delta(layer space) -> scale -> Z rotate -> parent space delta
//       -> 기존 Position 에 더함

function setAnchorPoint(h, v) {
    h = parseFloat(h);
    v = parseFloat(v);

    var comp = app.project.activeItem;
    if (!(comp instanceof CompItem)) return err("No active composition.");

    var selected = comp.selectedLayers;
    if (selected.length === 0) return err("No layers selected.");

    app.beginUndoGroup("Set Anchor Point");

    try {
        var count = 0;
        for (var i = 0; i < selected.length; i++) {
            _moveAnchor(selected[i], comp.time, h, v);
            count++;
        }
        app.endUndoGroup();
        return ok({ count: count });
    } catch (e) {
        app.endUndoGroup();
        return err(e.toString());
    }
}

function _moveAnchor(layer, time, h, v) {
    // 1) 레이어 바운딩 박스 (레이어 로컬 공간) — 마스크가 있으면 마스크 영역 기준
    var rect = _layerBoundsAtTime(layer, time);

    // 2) 새 Anchor Point 좌표
    var newAX = rect.left + rect.width  * h;
    var newAY = rect.top  + rect.height * v;

    var tg = layer.property("ADBE Transform Group");
    var apProp = tg.property("ADBE Anchor Point");

    var oldAP = apProp.valueAtTime(time, false);

    // 3) 레이어 공간 내 이동량
    var dX = newAX - oldAP[0];
    var dY = newAY - oldAP[1];

    // 4) Scale + Z-Rotation 으로 Parent 공간 이동량 변환
    var scale = tg.property("ADBE Scale").valueAtTime(time, false);
    var rot   = tg.property("ADBE Rotate Z").valueAtTime(time, false);
    var rad   = rot * Math.PI / 180;
    var cos   = Math.cos(rad);
    var sin_  = Math.sin(rad);
    var sx    = scale[0] / 100;
    var sy    = scale[1] / 100;

    var sdX = dX * sx;
    var sdY = dY * sy;
    var cdX = sdX * cos - sdY * sin_;
    var cdY = sdX * sin_ + sdY * cos;

    // 5) Anchor Point 설정
    apProp.setValue([newAX, newAY]);

    // 6) Position 보정 (일반 / Separate Dimensions 모두 처리)
    try {
        var posProp = tg.property("ADBE Position");
        var oldPos  = posProp.valueAtTime(time, false);
        if (layer.threeDLayer) {
            posProp.setValue([oldPos[0] + cdX, oldPos[1] + cdY, oldPos[2]]);
        } else {
            posProp.setValue([oldPos[0] + cdX, oldPos[1] + cdY]);
        }
    } catch (posErr) {
        // Separate Dimensions 모드 대응
        try {
            var xp = tg.property("ADBE Position_0");
            var yp = tg.property("ADBE Position_1");
            xp.setValue(xp.valueAtTime(time, false) + cdX);
            yp.setValue(yp.valueAtTime(time, false) + cdY);
        } catch (e2) {
            $.writeln("[setAnchorPoint] Position compensation failed: " + e2.toString());
        }
    }
}

// ── Screen Color Picker — AE 네이티브 방식 ────────────────────
//
//  executeCommand(2240) 기법 — 임시 Null + Color Control.
//  단, Null 을 shy + disabled 로 설정하고 comp.hideShyLayers = true
//  로 전환해 타임라인에 전혀 표시되지 않도록 처리.
//  색상 선택 완료 후 즉시 Null 삭제 및 hideShyLayers 원복.

function openAEColorPicker(initialHex) {
    // 활성 컴프 확인 -- 없으면 임시 컴프를 직접 생성해서 사용
    var activeItem    = app.project.activeItem;
    var usingTempComp = !(activeItem instanceof CompItem);
    var comp          = null;
    var tempComp      = null;
    var tempNull      = null;
    var nullSource    = null;
    var savedHideShy  = false;

    try {
        if (usingTempComp) {
            // 컴프 없음 -- 피킹 전용 임시 컴프 생성 (100x100, 1초, 30fps)
            tempComp = app.project.items.addComp("__cp_comp__", 100, 100, 1, 1, 30);
            comp = tempComp;
            // 새 컴프를 뷰어에 열어 타임라인·Effect Controls 패널이 활성화되도록 함.
            // openInViewer() 없이는 executeCommand(2240) 이 다이얼로그를 띄우지 못함.
            tempComp.openInViewer();
        } else {
            comp = activeItem;
            savedHideShy = comp.hideShyLayers;
        }

        // 초기 색상 파싱 (RRGGBB -> 0~1 범위)
        var ir = 0.298, ig = 0.686, ib = 0.314;
        var hexStr = String(initialHex).replace(/[^0-9a-fA-F]/g, "");
        if (hexStr.length === 6) {
            ir = parseInt(hexStr.substr(0, 2), 16) / 255;
            ig = parseInt(hexStr.substr(2, 2), 16) / 255;
            ib = parseInt(hexStr.substr(4, 2), 16) / 255;
        }

        // 기존 레이어 선택 해제
        for (var i = 1; i <= comp.numLayers; i++) {
            comp.layer(i).selected = false;
        }

        // 임시 Null 생성 (shy + disabled -- 타임라인에 표시 안 됨)
        tempNull         = comp.layers.addNull();
        nullSource       = tempNull.source;
        tempNull.name    = "__cp_temp__";
        tempNull.shy     = true;
        tempNull.enabled = false;
        comp.hideShyLayers = true;

        // Color Control 이펙트 추가 및 초기 색상 설정
        var fxList    = tempNull.property("ADBE Effect Parade");
        var fx        = fxList.addProperty("ADBE Color Control");
        var colorProp = fx.property(1);
        colorProp.setValue([ir, ig, ib, 1.0]);

        // Color 프로퍼티 선택 후 Edit Value 실행 (AE 네이티브 컬러 피커)
        tempNull.selected  = true;
        colorProp.selected = true;
        app.executeCommand(2240);
        // 다이얼로그가 열린 동안 이 줄에서 블록됨

        // 결과 색상 읽기
        var c = colorProp.value;

        // ── 정리 ──────────────────────────────────────────────
        // 활성 컴프 사용 시: hideShyLayers 원복
        if (!usingTempComp) {
            comp.hideShyLayers = savedHideShy;
        }

        // Null 레이어 및 소스 아이템 제거
        tempNull.remove();
        tempNull = null;
        try { nullSource.remove(); } catch (e2) {}
        nullSource = null;

        // 임시 컴프 제거 (임시 생성한 경우에만)
        if (usingTempComp) {
            try { tempComp.remove(); } catch (e3) {}
            tempComp = null;
        }
        // ──────────────────────────────────────────────────────

        // hex 변환
        var h2 = function(v) {
            var clamped = Math.max(0, Math.min(1, v));
            var dec = Math.round(clamped * 255);
            var s = dec.toString(16).toUpperCase();
            return (s.length < 2) ? "0" + s : s;
        };
        var resultHex = h2(c[0]) + h2(c[1]) + h2(c[2]);

        return ok({ hex: resultHex });

    } catch (e) {
        // 오류 시 생성된 모든 임시 객체 정리
        try { if (!usingTempComp) comp.hideShyLayers = savedHideShy; } catch (e2) {}
        if (tempNull   !== null) { try { tempNull.remove();   } catch (e3) {} }
        if (nullSource !== null) { try { nullSource.remove(); } catch (e4) {} }
        if (tempComp   !== null) { try { tempComp.remove();   } catch (e5) {} }
        return err(String(e));
    }
}

// ── Green Null Creator ────────────────────────────────────────
//
//  Project 내 Null 소스를 최초 1회만 생성하고,
//  이후 호출에서는 동일 소스를 재사용(인스턴스 추가)한다.
//  → Solids 폴더에 항상 하나의 "BANG_Null" 항목만 존재.

var NULL_SOURCE_NAME = "BANG_Null";

// Project 전체를 탐색해 지정 이름의 Null 소스를 찾아 반환.
// 없으면 null 반환.
// NullSource 클래스는 ExtendScript 에 미존재 → 이름으로만 식별.
function findNullSource() {
    for (var i = 1; i <= app.project.numItems; i++) {
        var item = app.project.item(i);
        if (item instanceof FootageItem && item.name === NULL_SOURCE_NAME) {
            return item;
        }
    }
    return null;
}

// 마스크가 있는 "모든" 선택 레이어의 마스크 영역 중심을 컴프 공간에서 구해
// 그 평균(centroid) 위치에 Null 을 배치한다.
//  · 마스크가 있는 선택 레이어만 집계한다. 하나도 없으면 false (위치 유지 = 기존 동작).
//  · 반드시 parent 연결 "이전"에 호출해야 한다 — 연결 후엔 toComp 가 순환된다.
//  레이어 공간 -> 컴프 공간 변환은 Null 의 Position 에 AE 표현식 toComp() 를 잠시 걸어
//  평가(부모/스케일/회전/3D 합성까지 AE 가 처리)한 뒤, 평균값을 정적으로 굳힌다.
//  인덱스 참조라 동명 레이어가 있어도 안전.
function _positionNullAtMaskCenter(nullLayer, refLayers, time) {
    var posProp = nullLayer.property("ADBE Transform Group").property("ADBE Position");

    var sumX = 0, sumY = 0, sumZ = 0, n = 0, any3D = false;

    try {
        for (var i = 0; i < refLayers.length; i++) {
            var refLayer = refLayers[i];
            var maskRect = _maskBoundsAtTime(refLayer, time);
            if (maskRect === null) continue;

            var cx = maskRect.left + maskRect.width  / 2;
            var cy = maskRect.top  + maskRect.height / 2;

            posProp.expression =
                'thisComp.layer(' + refLayer.index + ').toComp([' + cx + ',' + cy + ']);';
            var world = posProp.value;   // 표현식 평가 결과(컴프 공간)

            sumX += world[0];
            sumY += world[1];
            if (world.length > 2) { sumZ += world[2]; any3D = true; }
            n++;
        }
    } catch (e) {
        try { posProp.expression = ""; } catch (e2) {}
        return false;
    }

    posProp.expression = "";   // 표현식 제거 -> 정적 값으로 고정
    if (n === 0) return false;

    var avg = (nullLayer.threeDLayer && any3D)
        ? [sumX / n, sumY / n, sumZ / n]
        : [sumX / n, sumY / n];
    posProp.setValue(avg);
    return true;
}

function createGreenNull() {
    var comp = app.project.activeItem;

    if (!(comp instanceof CompItem)) {
        return err("No active composition.");
    }

    // 선택 레이어를 미리 수집
    var selected = [];
    var raw = comp.selectedLayers;
    for (var s = 0; s < raw.length; s++) {
        selected.push(raw[s]);
    }

    app.beginUndoGroup("Create Green Null");

    try {
        var nullLayer;
        var existingSource = findNullSource();
        // Null 추가 "전에" 이름을 계산한다: 새 Null 의 임시 기본명("Null 1")이
        // "Null " 접두사와 겹쳐 자기 자신을 카운트하는 off-by-one 을 방지.
        var newName = buildNullName(comp);

        if (existingSource !== null) {
            // ── 재사용 경로 ──────────────────────────────────────
            // Project에 새 항목을 만들지 않고 기존 소스를 레이어로 추가.
            // comp.layers.add() 는 FootageItem 을 인스턴스화한다.
            nullLayer = comp.layers.add(existingSource);
        } else {
            // ── 최초 생성 경로 ───────────────────────────────────
            // addNull() 이 Footage + Layer 를 동시에 생성.
            // 생성된 소스 이름을 고정해 다음 호출부터 findNullSource() 로 발견되도록 함.
            nullLayer = comp.layers.addNull();
            nullLayer.source.name = NULL_SOURCE_NAME;
        }

        nullLayer.name            = newName;
        nullLayer.label           = 9;    // Green
        nullLayer.adjustmentLayer = true; // Adjustment Layer 활성화

        nullLayer
            .property("ADBE Transform Group")
            .property("ADBE Anchor Point")
            .setValue([50, 50, 0]);

        // 마스크가 있는 모든 선택 레이어의 마스크 중심을 평균낸 위치에 Null 배치.
        // (부모 연결 전에 수행 — toComp 순환 방지. 마스크 없으면 기존 위치 유지)
        var maskCentered = _positionNullAtMaskCenter(nullLayer, selected, comp.time);

        var parented = 0;
        for (var i = 0; i < selected.length; i++) {
            if (selected[i].index !== nullLayer.index) {
                selected[i].parent = nullLayer;
                parented++;
            }
        }

        app.endUndoGroup();
        return ok({ name: nullLayer.name, parented: parented, maskCentered: maskCentered });

    } catch (e) {
        app.endUndoGroup();
        return err(e.toString());
    }
}


// ============================================================
//  Quote Align (따옴표 정렬) — 인용 자막 행잉 펑추에이션
//  선택 레이어 스마트 라우팅:
//   · 박스 텍스트  → hangingRoman + 높이 자동성장 세트 적용
//   · 포인트 텍스트 → 박스 레이어로 리빌드 변환 (첫 줄 고정 정합)
//   · 그 외        → 사유와 함께 건너뜀
//  규칙: "정렬 기둥은 첫 글자가 정의한다. 행두의 여는 부호는
//         개수와 무관하게 전부 여백으로 내어쓴다."
//  요구: AE 24.3+ (paragraphRange / hangingRoman 스크립팅)
// ============================================================

// ── Quote Align 설정 ─────────────────────────────────────────
var QH_ANCHOR_MODE   = "FIRSTLINE";        // "FIRSTLINE"(첫 줄 고정) | "BODY"(본문 고정)
var QH_OPTICAL_OFFSET = 0;                 // px. 폭 측정 폴백 보정
var QH_QUOTE_CHARS   = "\u201C\"\u2018'";  // 여는 따옴표로 인정할 문자
var QH_BOX_MARGIN_X  = 1.0;                // 박스 너비 여유 (폰트 크기 배수)
var QH_BOX_MARGIN_Y  = 1.0;
var QH_USE_AUTOFIT   = true;               // 박스 높이 자동 성장
var QH_REMOVE_ORIGINAL  = true;            // 원본 삭제 (새 박스 레이어가 원본 이름을 이어받음; 되돌리기는 Ctrl+Z)

// 완료 팝업 메시지 (자유롭게 수정)
var QH_MSG_DONE =
    "따옴표 정렬 완료!\n" +
    "\n" +
    "\u203B 주의\n" +
    "\u00B7 키프레임\u00B7이펙트\u00B7표현식은 새 레이어로 옮겨지지 않습니다.\n" +
    "\u00B7 복잡하게 세팅된 레이어는 결과가 어긋날 수 있습니다.\n" +
    "\u2192 본격적인 작업 전에 먼저 실행해 주세요.\n" +
    "\n" +
    "되돌리기: Ctrl+Z";

// ── Quote Align 유틸리티 ─────────────────────────────────────

function qh_try(fn) { try { fn(); return true; } catch (e) { return false; } }

function qh_firstValidLine(bl, startIdx) {
    var n = Math.floor(bl.length / 4);
    for (var i = startIdx; i < n; i++) { if (bl[i * 4] < 1e30) return i; }
    for (var j = 0; j < n; j++) { if (bl[j * 4] < 1e30) return j; }
    return -1;
}

// 박스 텍스트: 행잉 세트만 적용
function qh_applyBoxSet(ly, lines) {
    var prop = ly.property("Source Text");
    var doc = prop.value;
    doc.hangingRoman = true;
    if (QH_USE_AUTOFIT) {
        qh_try(function () {
            if (typeof BoxAutoFitPolicy !== "undefined") {
                doc.boxAutoFitPolicy = BoxAutoFitPolicy.HEIGHT_PRECISE_BOUNDS;
            }
        });
    }
    prop.setValue(doc);
    lines.push("박스 → 행잉 세트 적용");
    return null;
}

// 포인트 텍스트: 박스로 리빌드 (트랜스폼 원값 복사 + 레이어 공간 앵커 정합)
function qh_rebuildLayer(comp, orig, lines) {
    var oProp = orig.property("Source Text");
    var oDoc = oProp.value;
    var warns = [];

    function hasKeys(name) {
        try { return orig.property(name).numKeys > 0; } catch (e) { return false; }
    }
    var is3D = false;
    qh_try(function () { is3D = orig.threeDLayer === true; });
    var trNames = ["Position", "Anchor Point", "Scale", "Rotation",
                   "X Rotation", "Y Rotation", "Orientation", "Opacity"];
    for (var ti = 0; ti < trNames.length; ti++) {
        if (hasKeys(trNames[ti])) warns.push(trNames[ti] + " 키프레임 — 값만 복사");
    }
    try {
        var fx = orig.property("ADBE Effect Parade");
        if (fx && fx.numProperties > 0) warns.push("이펙트 " + fx.numProperties + "개 미이전");
    } catch (e4) {}
    try {
        var anims = orig.property("ADBE Text Properties").property("ADBE Text Animators");
        if (anims && anims.numProperties > 0) warns.push("애니메이터 " + anims.numProperties + "개 미이전");
    } catch (e5) {}

    var oBL = null;
    try { oBL = oDoc.baselineLocs; } catch (eB) {}
    var canAlign = (oBL !== null && oBL.length >= 4);
    if (!canAlign) warns.push("baselineLocs 실패 — 정합 생략");
    var oIdx0 = canAlign ? qh_firstValidLine(oBL, 0) : -1;
    var oIdxBody = canAlign ? qh_firstValidLine(oBL, 1) : -1;
    if (canAlign && oIdx0 < 0) { warns.push("유효 줄 없음 — 정합 생략"); canAlign = false; }

    var origText = oDoc.text;
    var quoteChar = (origText.length > 0) ? origText.charAt(0) : "";
    var isQuote = (quoteChar !== "" && QH_QUOTE_CHARS.indexOf(quoteChar) !== -1);
    var firstBody = null;
    if (isQuote) {
        var restT = origText.substring(1);
        for (var fi = 0; fi < restT.length; fi++) {
            var fc = restT.charAt(fi);
            if (fc !== "\r" && fc !== "\n" && fc !== " ") { firstBody = fc; break; }
        }
        if (firstBody === null) isQuote = false;
    }

    var rect = null, fs = 50;
    qh_try(function () { fs = oDoc.fontSize; });
    qh_try(function () { rect = orig.sourceRectAtTime(comp.time, false); });
    var boxW = rect ? Math.ceil(rect.width + fs * QH_BOX_MARGIN_X) : Math.ceil(fs * 20);
    var boxH = rect ? Math.ceil(rect.height + fs * QH_BOX_MARGIN_Y) : Math.ceil(fs * 8);

    var nl = comp.layers.addBoxText([boxW, boxH]);
    nl.name = orig.name + " [BOX]";
    nl.moveBefore(orig);

    qh_try(function () { if (orig.parent !== null) nl.parent = orig.parent; });
    if (is3D) qh_try(function () { nl.threeDLayer = true; });
    function copyProp(name) {
        return qh_try(function () { nl.property(name).setValue(orig.property(name).value); });
    }
    copyProp("Position");
    copyProp("Anchor Point");
    copyProp("Scale");
    copyProp("Rotation");
    if (is3D) { copyProp("X Rotation"); copyProp("Y Rotation"); copyProp("Orientation"); }
    copyProp("Opacity");
    qh_try(function () { nl.blendingMode = orig.blendingMode; });

    var nProp = nl.property("Source Text");
    var nDoc = nProp.value;
    qh_try(function () { nDoc.font = oDoc.font; });
    qh_try(function () { nDoc.fontSize = oDoc.fontSize; });
    qh_try(function () { nDoc.applyFill = oDoc.applyFill; });
    qh_try(function () { if (oDoc.applyFill) nDoc.fillColor = oDoc.fillColor; });
    qh_try(function () { nDoc.applyStroke = oDoc.applyStroke; });
    qh_try(function () {
        if (oDoc.applyStroke) {
            nDoc.strokeColor = oDoc.strokeColor;
            nDoc.strokeWidth = oDoc.strokeWidth;
            nDoc.strokeOverFill = oDoc.strokeOverFill;
        }
    });
    qh_try(function () { nDoc.tracking = oDoc.tracking; });
    qh_try(function () {
        if (oDoc.autoLeading) { nDoc.autoLeading = true; }
        else { nDoc.leading = oDoc.leading; }
    });
    qh_try(function () { nDoc.justification = oDoc.justification; });
    qh_try(function () { nDoc.fauxBold = oDoc.fauxBold; });
    qh_try(function () { nDoc.fauxItalic = oDoc.fauxItalic; });
    qh_try(function () { nDoc.horizontalScale = oDoc.horizontalScale; });
    qh_try(function () { nDoc.verticalScale = oDoc.verticalScale; });
    qh_try(function () { nDoc.baselineShift = oDoc.baselineShift; });
    qh_try(function () { nDoc.tsume = oDoc.tsume; });
    nDoc.text = origText;
    nDoc.hangingRoman = true;
    if (QH_USE_AUTOFIT) {
        qh_try(function () {
            if (typeof BoxAutoFitPolicy !== "undefined") {
                nDoc.boxAutoFitPolicy = BoxAutoFitPolicy.HEIGHT_PRECISE_BOUNDS;
            }
        });
    }
    nProp.setValue(nDoc);

    var nDoc2 = nProp.value;
    var nBL = null, btp = null;
    qh_try(function () { nBL = nDoc2.baselineLocs; });
    qh_try(function () { btp = nDoc2.boxTextPos; });

    // 따옴표 폭: 엔진 보고값 우선, 실패 시 차분 측정 폴백
    var qWidth = 0;
    if (QH_ANCHOR_MODE === "FIRSTLINE" && isQuote) {
        var nIdx0a = (nBL !== null) ? qh_firstValidLine(nBL, 0) : -1;
        var gotEngine = false;
        if (btp !== null && nIdx0a >= 0) {
            var qEng = btp[0] - nBL[nIdx0a * 4];
            if (qEng > 0.5) { qWidth = qEng; gotEngine = true; }
        }
        if (!gotEngine) {
            try {
                nDoc.text = quoteChar + firstBody;
                nProp.setValue(nDoc);
                var wA = nl.sourceRectAtTime(comp.time, false).width;
                nDoc.text = firstBody;
                nProp.setValue(nDoc);
                var wB = nl.sourceRectAtTime(comp.time, false).width;
                qWidth = (wA - wB) + QH_OPTICAL_OFFSET;
                if (qWidth < 0) qWidth = 0;
                nDoc.text = origText;
                nProp.setValue(nDoc);
                nDoc2 = nProp.value;
                nBL = null; btp = null;
                qh_try(function () { nBL = nDoc2.baselineLocs; });
                qh_try(function () { btp = nDoc2.boxTextPos; });
            } catch (eQ) {
                warns.push("따옴표 폭 확보 실패 — 폭 0");
                qWidth = 0;
            }
        }
    }

    // 정합: anchor_new = anchor(복사값) + (p_new − p_old)
    var aligned = false;
    if (canAlign && nBL !== null) {
        try {
            var oLines = Math.floor(oBL.length / 4);
            var nLines = Math.floor(nBL.length / 4);
            if (nLines !== oLines) warns.push("줄 수 변화(" + oLines + "→" + nLines + ")");
            var nIdx0 = qh_firstValidLine(nBL, 0);
            var pOld = null, pNew = null;

            if (QH_ANCHOR_MODE === "FIRSTLINE") {
                if (btp !== null && nIdx0 >= 0 && oIdx0 >= 0) {
                    pOld = [oBL[oIdx0 * 4] + qWidth, oBL[oIdx0 * 4 + 1]];
                    pNew = [btp[0], nBL[nIdx0 * 4 + 1]];
                }
            } else {
                var oI = (oIdxBody >= 0) ? oIdxBody : oIdx0;
                var nI = qh_firstValidLine(nBL, (oI <= nLines - 1) ? oI : 1);
                if (oI >= 0 && nI >= 0) {
                    pOld = [oBL[oI * 4], oBL[oI * 4 + 1]];
                    pNew = [nBL[nI * 4], nBL[nI * 4 + 1]];
                }
            }

            if (pOld !== null && pNew !== null) {
                var ap = nl.property("Anchor Point");
                var av = ap.value;
                var nv = [];
                for (var k = 0; k < av.length; k++) nv[k] = av[k];
                nv[0] = nv[0] + (pNew[0] - pOld[0]);
                nv[1] = nv[1] + (pNew[1] - pOld[1]);
                ap.setValue(nv);
                aligned = true;
            } else {
                warns.push("정합 기준점 실패 — 근사 배치");
            }
        } catch (eAl) {
            warns.push("정합 실패: " + eAl.toString());
        }
    }

    qh_try(function () { nl.startTime = orig.startTime; });
    qh_try(function () { nl.inPoint = orig.inPoint; });
    qh_try(function () { nl.outPoint = orig.outPoint; });
    qh_try(function () { nl.label = orig.label; });
    if (QH_REMOVE_ORIGINAL) {
        var keepName = orig.name;
        orig.remove();                 // undo 그룹 안이므로 Ctrl+Z 한 번으로 복원됨
        nl.name = keepName;
    }

    qh_try(function () { if (nDoc2.boxOverflow === true) warns.push("박스 넘침 — 여유 상수 증가 요망"); });
    lines.push("포인트 → 박스 변환" + (aligned ? "" : " (정합 생략)"));
    for (var w = 0; w < warns.length; w++) lines.push("주의: " + warns[w]);
    return nl;
}

function qh_routeLayer(comp, ly, lines) {
    if (!(ly instanceof TextLayer)) {
        lines.push("건너뜀: 텍스트 레이어 아님");
        return { made: null, skipped: true };
    }
    var prop = ly.property("Source Text");
    if (prop.numKeys > 0) {
        lines.push("건너뜀: 소스텍스트 키프레임 (UI 수동 변환 권장)");
        return { made: null, skipped: true };
    }
    var doc = prop.value;
    if (doc.boxText) return { made: qh_applyBoxSet(ly, lines), skipped: false, boxSet: true };
    return { made: qh_rebuildLayer(comp, ly, lines), skipped: false, boxSet: false };
}

// ── Quote Align 메인 (패널 버튼 진입점) ──────────────────────
function applyQuoteHang() {
    var comp = app.project ? app.project.activeItem : null;
    if (!(comp && comp instanceof CompItem)) return err("활성 컴프가 없습니다.");
    if (parseFloat(app.version) < 24.3) return err("AE 2024(24.3) 이상에서 동작합니다. 현재: " + app.version);

    // 선택 스냅샷 (처리 중 선택 상태가 변하므로 필수)
    var sel = [];
    for (var si = 0; si < comp.selectedLayers.length; si++) sel.push(comp.selectedLayers[si]);
    if (sel.length === 0) return err("처리할 레이어를 선택해 주세요.");

    var rebuilt = 0, boxSet = 0, skipped = 0, errors = 0;
    var created = [];
    var issueLines = [];

    app.beginUndoGroup("BANG Quote Align");
    try {
        for (var i = 0; i < sel.length; i++) {
            var lines = [];
            var lyName = sel[i].name;  // 원본이 삭제될 수 있으므로 미리 확보
            try {
                var r = qh_routeLayer(comp, sel[i], lines);
                if (r.skipped) skipped++;
                else if (r.boxSet) boxSet++;
                else rebuilt++;
                if (r.made !== null) created.push(r.made);
            } catch (eL) {
                errors++;
                lines.push("오류: " + eL.toString());
            }
            // 경고·건너뜀·오류가 있는 레이어만 팝업에 표시 (최대 6줄)
            for (var li = 0; li < lines.length; li++) {
                if (issueLines.length < 6 &&
                    (lines[li].indexOf("주의") === 0 ||
                     lines[li].indexOf("건너뜀") === 0 ||
                     lines[li].indexOf("오류") === 0)) {
                    issueLines.push(lyName + " — " + lines[li]);
                }
            }
        }
    } catch (eMain) {
        app.endUndoGroup();
        return err(eMain.toString());
    }
    app.endUndoGroup();

    // 생성된 [BOX] 레이어만 선택 상태로
    qh_try(function () {
        for (var di = 1; di <= comp.numLayers; di++) comp.layer(di).selected = false;
        for (var ci = 0; ci < created.length; ci++) created[ci].selected = true;
    });

    // 완료 팝업 (undo 그룹 종료 후 — 크래시 완화)
    if (rebuilt + boxSet > 0) {
        var msg = QH_MSG_DONE;
        if (issueLines.length > 0) msg += "\n\n" + issueLines.join("\n");
        alert(msg);
    }

    return ok({ rebuilt: rebuilt, boxSet: boxSet, skipped: skipped, errors: errors });
}
