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

// 선택 레이어들의 "기준 영역" 중심을 컴프 공간에서 구해 그 평균(centroid) 위치에 Null 을 배치한다.
//  · 기준 영역 = 마스크가 있으면 마스크 영역, 없으면 레이어(오브젝트) 경계 (_layerBoundsAtTime).
//  · 경계를 구할 수 없는 레이어(카메라·라이트 등)는 건너뛴다. 집계된 레이어가 없으면 false (컴프 중앙 유지).
//  · 반드시 parent 연결 "이전"에 호출해야 한다 — 연결 후엔 toComp 가 순환된다.
//  레이어 공간 -> 컴프 공간 변환은 Null 의 Position 에 AE 표현식 toComp() 를 잠시 걸어
//  평가(부모/스케일/회전/3D 합성까지 AE 가 처리)한 뒤, 평균값을 정적으로 굳힌다.
//  인덱스 참조라 동명 레이어가 있어도 안전.
function _positionNullAtSelectionCenter(nullLayer, refLayers, time) {
    var posProp = nullLayer.property("ADBE Transform Group").property("ADBE Position");

    var sumX = 0, sumY = 0, sumZ = 0, n = 0, any3D = false;

    try {
        for (var i = 0; i < refLayers.length; i++) {
            var refLayer = refLayers[i];
            var rect = null;
            try { rect = _layerBoundsAtTime(refLayer, time); } catch (eRect) { rect = null; }
            if (rect === null) continue;

            var cx = rect.left + rect.width  / 2;
            var cy = rect.top  + rect.height / 2;

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

        // 선택 레이어들의 (마스크 또는 오브젝트) 중심을 평균낸 위치에 Null 배치.
        // (부모 연결 전에 수행 — toComp 순환 방지. 선택이 없으면 컴프 중앙 유지)
        var centered = _positionNullAtSelectionCenter(nullLayer, selected, comp.time);

        // 레이어 순서: 컴프 최상단이 아니라 선택 레이어 중 최상단 바로 위에 둔다.
        // (Null 추가로 선택 레이어들의 index 는 이미 +1 된 상태)
        if (selected.length > 0) {
            var topIndex = selected[0].index;
            for (var t = 1; t < selected.length; t++) {
                if (selected[t].index < topIndex) topIndex = selected[t].index;
            }
            nullLayer.moveBefore(comp.layer(topIndex));
        }

        var parented = 0;
        for (var i = 0; i < selected.length; i++) {
            if (selected[i].index !== nullLayer.index) {
                selected[i].parent = nullLayer;
                parented++;
            }
        }

        app.endUndoGroup();
        return ok({ name: nullLayer.name, parented: parented, centered: centered, index: nullLayer.index });

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


// ============================================================
//  Precomp Fit — 프리컴프 크기를 내부 레이어 경계에 맞춤
//  · 진입: 부모 컴프에서 프리컴프 레이어를 선택(밖에서) 또는
//          프리컴프를 활성 컴프로 열고 선택 없이(안에서) 실행
//  · 내부 레이어 경계(sourceRect 4모서리 → toComp) 합집합 + 여백으로 컴프 크기 변경
//  · 내부 최상위 레이어 Position(전체 키) 오프셋 → 내용 위치 유지
//  · 이 프리컴프를 쓰는 모든 인스턴스 레이어의 Anchor Point(전체 키) 오프셋
//    → 부모 컴프에서 스케일·회전이 있어도 시각적 위치 불변
//  · 시간 범위: "current"(현재 프레임) | "all"(워크에어리어 샘플링)
// ============================================================

var PF_MAX_SAMPLES = 120;   // "all" 모드 최대 샘플 프레임 수
var PF_MIN_SIZE    = 4;
var PF_MAX_SIZE    = 30000;

// 임시 Null 의 Position 표현식으로 레이어 로컬 좌표 → 컴프 좌표 변환
function _pfToComp(probeProp, layerIndex, x, y, time) {
    probeProp.expression = "thisComp.layer(" + layerIndex + ").toComp([" + x + "," + y + "])";
    var v = probeProp.valueAtTime(time, false);
    return [v[0], v[1]];
}

// 레이어의 최대 Stroke 두께 / 2 (레이어 공간 px). 셰이프: 모든 Stroke 그래픽의 Width 최대값, 텍스트: strokeWidth
function _pfMaxStrokeHalf(layer, time) {
    var maxW = 0;
    function walk(grp) {
        for (var i = 1; i <= grp.numProperties; i++) {
            var pr = grp.property(i);
            if (!pr) continue;
            if (pr.matchName === "ADBE Vector Graphic - Stroke") {
                try { if (pr.enabled !== false) maxW = Math.max(maxW, pr.property("ADBE Vector Stroke Width").valueAtTime(time, false)); } catch (e1) {}
            } else if (pr.propertyType !== undefined && pr.propertyType !== PropertyType.PROPERTY) {
                try { walk(pr); } catch (e2) {}
            }
        }
    }
    try {
        if (layer instanceof ShapeLayer) walk(layer.property("ADBE Root Vectors Group"));
        else if (layer instanceof TextLayer) {
            var d = layer.property("Source Text").valueAtTime(time, false);
            if (d.applyStroke) maxW = Math.max(maxW, d.strokeWidth);
        }
    } catch (e) {}
    return maxW / 2;
}

function _pfIsMeasurable(layer) {
    if (layer instanceof CameraLayer || layer instanceof LightLayer) return false;
    if (!layer.enabled) return false;
    try { if (layer.guideLayer) return false; } catch (e) {}
    try { if (layer.adjustmentLayer) return false; } catch (e2) {}
    return true;
}

// 컴프 내부 레이어들의 합집합 bbox (컴프 공간). 없으면 null.
function _pfContentBounds(comp, mode, warns) {
    var times = [];
    if (mode === "all") {
        var start = comp.workAreaStart, dur = comp.workAreaDuration, fd = comp.frameDuration;
        var frames = Math.round(dur / fd);
        var step = Math.max(1, Math.ceil(frames / PF_MAX_SAMPLES));
        for (var f = 0; f <= frames; f += step) times.push(start + f * fd);
        if (times[times.length - 1] < start + dur - fd / 2) times.push(start + dur - fd);
    } else {
        times.push(comp.time);
    }

    var probe = comp.layers.addNull();
    probe.name = "__BANG_PF_PROBE__";
    var probeProp = probe.property("ADBE Transform Group").property("ADBE Position");
    var minX, minY, maxX, maxY, found = false;
    var has3D = false, hasCam = false;

    try {
        for (var i = 1; i <= comp.numLayers; i++) {
            var ly = comp.layer(i);
            if (ly === probe) continue;
            if (ly instanceof CameraLayer) { hasCam = true; continue; }
            if (!_pfIsMeasurable(ly)) continue;
            if (ly.threeDLayer) has3D = true;
            for (var t = 0; t < times.length; t++) {
                var tm = times[t];
                if (tm < ly.inPoint || tm > ly.outPoint) continue;
                var r;
                try { r = ly.sourceRectAtTime(tm, false); } catch (eR) { continue; }
                // Stroke 는 sourceRect 에 포함되지 않으므로 최대 두께의 절반만큼 경계를 넓힌다
                // (extents=true 는 AE 가 과하게 넉넉한 박스를 돌려줘 사용하지 않음)
                var sw = _pfMaxStrokeHalf(ly, tm);
                if (sw > 0) r = { left: r.left - sw, top: r.top - sw, width: r.width + sw * 2, height: r.height + sw * 2 };
                if (!r || !(r.width > 0) || !(r.height > 0)) continue;
                var cs = [[r.left, r.top], [r.left + r.width, r.top],
                          [r.left, r.top + r.height], [r.left + r.width, r.top + r.height]];
                for (var k = 0; k < 4; k++) {
                    var p = _pfToComp(probeProp, ly.index, cs[k][0], cs[k][1], tm);
                    if (!found) { minX = maxX = p[0]; minY = maxY = p[1]; found = true; }
                    else {
                        if (p[0] < minX) minX = p[0];
                        if (p[0] > maxX) maxX = p[0];
                        if (p[1] < minY) minY = p[1];
                        if (p[1] > maxY) maxY = p[1];
                    }
                }
            }
        }
    } finally {
        probe.remove();
    }
    if (has3D && !hasCam) warns.push(comp.name + ": 3D 레이어가 있고 카메라가 없음 — 기본 카메라 기준이 바뀌어 원근이 달라질 수 있음");
    if (!found) return null;
    return { left: minX, top: minY, right: maxX, bottom: maxY };
}

// 프로퍼티 값(전체 키 포함)에 [dx,dy] 오프셋
function _pfOffsetProp(prop, dx, dy, warns, label) {
    if (!prop) return;
    if (prop.expressionEnabled) { warns.push(label + ": 표현식이 있어 오프셋 생략"); return; }
    function shifted(v) {
        var o = [];
        for (var i = 0; i < v.length; i++) o[i] = v[i];
        o[0] += dx; if (o.length > 1) o[1] += dy;
        return o;
    }
    if (prop.numKeys > 0) {
        for (var k = 1; k <= prop.numKeys; k++) prop.setValueAtKey(k, shifted(prop.keyValue(k)));
    } else {
        prop.setValue(shifted(prop.value));
    }
}

function _pfOffsetLayer(ly, dx, dy, warns) {
    var tg = ly.property("ADBE Transform Group");
    var pos = tg.property("ADBE Position");
    if (pos.dimensionsSeparated) {
        _pfOffsetProp(tg.property("ADBE Position_0"), dx, 0, warns, ly.name + " X");
        _pfOffsetProp(tg.property("ADBE Position_1"), dy, 0, warns, ly.name + " Y");
    } else {
        _pfOffsetProp(pos, dx, dy, warns, ly.name + " Position");
    }
    if (ly instanceof CameraLayer || ly instanceof LightLayer) {
        // 카메라/라이트의 Point of Interest 도 함께 이동 (Auto-Orient 가 Off 면 ADBE Anchor Point 가 비활성)
        var poi = tg.property("ADBE Anchor Point");
        try { if (poi && poi.canSetExpression) _pfOffsetProp(poi, dx, dy, warns, ly.name + " POI"); } catch (e) {}
    }
}

function _pfFitOne(target, margin, mode, warns) {
    var b = _pfContentBounds(target, mode, warns);
    if (b === null) { warns.push(target.name + ": 측정 가능한 레이어 없음 — 건너뜀"); return null; }

    var left = Math.floor(b.left - margin), top = Math.floor(b.top - margin);
    var w = Math.ceil(b.right + margin) - left, h = Math.ceil(b.bottom + margin) - top;
    if (w < PF_MIN_SIZE) w = PF_MIN_SIZE;
    if (h < PF_MIN_SIZE) h = PF_MIN_SIZE;
    if (w > PF_MAX_SIZE || h > PF_MAX_SIZE) { warns.push(target.name + ": 크기 한도 초과(" + w + "x" + h + ") — 건너뜀"); return null; }
    var dx = -left, dy = -top;
    var from = [target.width, target.height];

    // 인스턴스 Anchor Point 스냅샷 — 컴프 크기를 바꾸면 AE 가 인스턴스 앵커를
    // 새 중앙으로 자동 재설정하므로, 리사이즈 "전" 값을 확보해 두고 나중에 +오프셋으로 명시 설정한다.
    var snaps = [];
    for (var j = 1; j <= app.project.numItems; j++) {
        var it = app.project.item(j);
        if (!(it instanceof CompItem) || it === target) continue;
        for (var L = 1; L <= it.numLayers; L++) {
            var inst = it.layer(L);
            if (inst.source !== target) continue;
            var ap = inst.property("ADBE Transform Group").property("ADBE Anchor Point");
            var snap = { prop: ap, label: it.name + "/" + inst.name + " Anchor", keys: [], value: null };
            if (ap.numKeys > 0) { for (var k = 1; k <= ap.numKeys; k++) snap.keys.push(ap.keyValue(k)); }
            else snap.value = ap.value;
            snaps.push(snap);
        }
    }

    target.width = w; target.height = h;

    // 내부 최상위 레이어(부모 없음)만 이동 — 자식은 따라옴
    for (var i = 1; i <= target.numLayers; i++) {
        var ly = target.layer(i);
        if (ly.parent !== null) continue;
        _pfOffsetLayer(ly, dx, dy, warns);
    }

    // 인스턴스 Anchor Point = 스냅샷 + 오프셋 (스케일·회전이 있어도 시각적 위치 불변)
    function shifted(v) { var o = []; for (var q = 0; q < v.length; q++) o[q] = v[q]; o[0] += dx; o[1] += dy; return o; }
    for (var sI = 0; sI < snaps.length; sI++) {
        var sn = snaps[sI];
        if (sn.prop.expressionEnabled) { warns.push(sn.label + ": 표현식이 있어 오프셋 생략"); continue; }
        if (sn.keys.length > 0) { for (var kk = 0; kk < sn.keys.length; kk++) sn.prop.setValueAtKey(kk + 1, shifted(sn.keys[kk])); }
        else sn.prop.setValue(shifted(sn.value));
    }
    return { name: target.name, from: from, to: [w, h], offset: [dx, dy], instances: snaps.length };
}

// 패널 진입점. margin(px), mode "current"|"all"
function fitPrecomp(margin, mode) {
    var comp = app.project ? app.project.activeItem : null;
    if (!(comp && comp instanceof CompItem)) return err("활성 컴프가 없습니다.");
    margin = parseFloat(margin); if (isNaN(margin) || margin < 0) margin = 0;
    mode = (mode === "all") ? "all" : "current";

    // 대상 수집: 선택된 프리컴프 레이어들(중복 제거) → 없으면 활성 컴프 자신
    var targets = [];
    var sel = comp.selectedLayers;
    for (var i = 0; i < sel.length; i++) {
        var src = null;
        try { src = sel[i].source; } catch (e) {}
        if (src instanceof CompItem) {
            var dup = false;
            for (var d = 0; d < targets.length; d++) if (targets[d] === src) dup = true;
            if (!dup) targets.push(src);
        }
    }
    var fromOutside = targets.length > 0;
    if (!fromOutside) {
        if (sel.length > 0) return err("선택한 레이어가 프리컴프가 아닙니다. 프리컴프 레이어를 선택하거나, 선택을 해제하고 프리컴프 안에서 실행하세요.");
        targets.push(comp);
    }

    var results = [], warns = [];
    app.beginUndoGroup("BANG Precomp Crop");
    try {
        for (var t = 0; t < targets.length; t++) {
            var r = _pfFitOne(targets[t], margin, mode, warns);
            if (r !== null) results.push(r);
        }
    } catch (eMain) {
        app.endUndoGroup();
        return err(eMain.toString());
    }
    app.endUndoGroup();
    if (results.length === 0) return err(warns.length ? warns.join(" / ") : "맞출 대상이 없습니다.");
    return ok({ fitted: results, warnings: warns, fromOutside: fromOutside, mode: mode, margin: margin });
}


// ============================================================
//  Align 3D — 3D 레이어가 켜져 있어도 동작하는 정렬/분배
//  · 각 레이어의 기준 영역(마스크 or sourceRect) 4모서리를
//    3D 레이어는 toWorld, 2D 레이어는 toComp 로 변환 → 축별 min/max (AABB)
//  · 기준: "selection"(선택 영역 합집합) | "comp"(컴프 0..W / 0..H / Z=0)
//  · 이동량(월드 벡터)은 부모가 있으면 부모의 fromWorldVec/fromCompVec 로
//    부모 공간 벡터로 바꿔 Position 에 더한다 → 부모·회전·스케일이 있어도 정확
//  · 축: "x" | "y" | "z",  모드: "min" | "center" | "max" | "distribute"
//  · Z 축은 3D 레이어에만 적용(2D 레이어는 Z 이동 불가 → 건너뜀)
// ============================================================

function _alBounds(comp, probeProp, ly, time) {
    var rect;
    try { rect = _layerBoundsAtTime(ly, time); } catch (e) { return null; }
    if (!rect) return null;
    var is3D = false;
    try { is3D = ly.threeDLayer === true; } catch (e2) {}
    var fn = is3D ? "toWorld" : "toComp";
    var cs = [[rect.left, rect.top], [rect.left + rect.width, rect.top],
              [rect.left, rect.top + rect.height], [rect.left + rect.width, rect.top + rect.height]];
    var b = null;
    for (var k = 0; k < 4; k++) {
        probeProp.expression = "thisComp.layer(" + ly.index + ")." + fn + "([" + cs[k][0] + "," + cs[k][1] + ",0])";
        var v = probeProp.valueAtTime(time, false);
        var p = [v[0], v[1], (v.length > 2 ? v[2] : 0)];
        if (b === null) b = { min: [p[0], p[1], p[2]], max: [p[0], p[1], p[2]] };
        else for (var a = 0; a < 3; a++) { if (p[a] < b.min[a]) b.min[a] = p[a]; if (p[a] > b.max[a]) b.max[a] = p[a]; }
    }
    b.is3D = is3D;
    b.center = [(b.min[0] + b.max[0]) / 2, (b.min[1] + b.max[1]) / 2, (b.min[2] + b.max[2]) / 2];
    return b;
}

// 월드(또는 컴프) 벡터 delta 를 레이어 Position 에 적용
function _alMoveLayer(comp, probeProp, ly, is3D, delta, time, warns) {
    var vec = [delta[0], delta[1], delta[2]];
    if (ly.parent !== null) {
        var fn = is3D ? "fromWorldVec" : "fromCompVec";
        probeProp.expression = "thisComp.layer(" + ly.parent.index + ")." + fn + "([" + vec[0] + "," + vec[1] + "," + vec[2] + "])";
        var pv = probeProp.valueAtTime(time, false);
        vec = [pv[0], pv[1], (pv.length > 2 ? pv[2] : 0)];
    }
    var tg = ly.property("ADBE Transform Group");
    var pos = tg.property("ADBE Position");
    function apply(prop, add) {
        if (!prop || Math.abs(add) < 1e-9) return;
        if (prop.expressionEnabled) { warns.push(ly.name + ": Position 표현식 — 생략"); return; }
        var v = prop.value;
        if (prop.numKeys > 0) {
            if (typeof v === "number") prop.setValueAtTime(time, v + add);
            else { var o = []; for (var i = 0; i < v.length; i++) o[i] = v[i]; o[0] += add; prop.setValueAtTime(time, o); }
        } else {
            if (typeof v === "number") prop.setValue(v + add);
            else { var o2 = []; for (var j = 0; j < v.length; j++) o2[j] = v[j]; o2[0] += add; prop.setValue(o2); }
        }
    }
    if (pos.dimensionsSeparated) {
        apply(tg.property("ADBE Position_0"), vec[0]);
        apply(tg.property("ADBE Position_1"), vec[1]);
        if (is3D) apply(tg.property("ADBE Position_2"), vec[2]);
    } else {
        if (pos.expressionEnabled) { warns.push(ly.name + ": Position 표현식 — 생략"); return; }
        var cur = pos.value;
        var nv = [];
        for (var q = 0; q < cur.length; q++) nv[q] = cur[q];
        nv[0] += vec[0]; nv[1] += vec[1];
        if (nv.length > 2 && is3D) nv[2] += vec[2];
        if (pos.numKeys > 0) pos.setValueAtTime(time, nv); else pos.setValue(nv);
    }
}

// 패널 진입점
//   axis: "x"|"y"|"z"   mode: "min"|"center"|"max"|"distribute"   ref: "selection"|"comp"
function alignLayers(axis, mode, ref) {
    var comp = app.project ? app.project.activeItem : null;
    if (!(comp && comp instanceof CompItem)) return err("활성 컴프가 없습니다.");
    // 주의: ExtendScript 는 중첩 삼항(a ? x : b ? y : z)을 잘못 파싱하므로 if/else 사용
    var ai = 0;
    if (axis === "y") ai = 1;
    else if (axis === "z") ai = 2;
    ref = (ref === "comp") ? "comp" : "selection";

    var sel = [];
    for (var s = 0; s < comp.selectedLayers.length; s++) {
        var L = comp.selectedLayers[s];
        if (L instanceof CameraLayer || L instanceof LightLayer) continue;
        sel.push(L);
    }
    if (sel.length === 0) return err("정렬할 레이어를 선택해 주세요.");
    if (mode === "distribute" && sel.length < 3) return err("분배는 3개 이상 선택이 필요합니다.");
    if (mode !== "distribute" && ref === "selection" && sel.length < 2) return err("선택 영역 기준 정렬은 2개 이상 선택이 필요합니다.");

    var time = comp.time, warns = [], moved = 0, skipped = 0;
    app.beginUndoGroup("BANG Align " + axis.toUpperCase() + " " + mode);
    var probe = comp.layers.addNull();
    probe.name = "__BANG_AL_PROBE__";
    probe.threeDLayer = true;   // Position 이 3차원이어야 toWorld/fromWorldVec 의 Z 가 보존됨
    var probeProp = probe.property("ADBE Transform Group").property("ADBE Position");
    try {
        // 1) 측정
        var items = [];
        for (var i = 0; i < sel.length; i++) {
            var b = _alBounds(comp, probeProp, sel[i], time);
            if (b === null) { skipped++; continue; }
            if (ai === 2 && !b.is3D) { skipped++; continue; }   // Z 는 3D 레이어만
            items.push({ layer: sel[i], b: b });
        }
        if (items.length === 0) throw new Error(ai === 2 ? "Z 정렬은 3D 레이어에만 적용됩니다." : "측정 가능한 레이어가 없습니다.");

        // 2) 기준 범위
        var refMin, refMax;
        if (ref === "comp") {
            refMin = [0, 0, 0][ai]; refMax = [comp.width, comp.height, 0][ai];
        } else {
            refMin = items[0].b.min[ai]; refMax = items[0].b.max[ai];
            for (var r = 1; r < items.length; r++) {
                if (items[r].b.min[ai] < refMin) refMin = items[r].b.min[ai];
                if (items[r].b.max[ai] > refMax) refMax = items[r].b.max[ai];
            }
        }

        // 3) 이동
        if (mode === "distribute") {
            items.sort(function (p, q) { return p.b.center[ai] - q.b.center[ai]; });
            var first = items[0].b.center[ai], last = items[items.length - 1].b.center[ai];
            if (ref === "comp") { first = refMin; last = refMax; }
            var n = items.length;
            for (var d = 0; d < n; d++) {
                var targetC = (ref === "comp")
                    ? first + (last - first) * (d + 1) / (n + 1)
                    : first + (last - first) * d / (n - 1);
                var dd = [0, 0, 0]; dd[ai] = targetC - items[d].b.center[ai];
                if (Math.abs(dd[ai]) > 1e-6) { _alMoveLayer(comp, probeProp, items[d].layer, items[d].b.is3D, dd, time, warns); moved++; }
            }
        } else {
            for (var m = 0; m < items.length; m++) {
                var bb = items[m].b, cur, tgt;
                if (mode === "min")      { cur = bb.min[ai];    tgt = refMin; }
                else if (mode === "max") { cur = bb.max[ai];    tgt = refMax; }
                else                     { cur = bb.center[ai]; tgt = (refMin + refMax) / 2; }
                var dv = [0, 0, 0]; dv[ai] = tgt - cur;
                if (Math.abs(dv[ai]) > 1e-6) { _alMoveLayer(comp, probeProp, items[m].layer, bb.is3D, dv, time, warns); moved++; }
            }
        }
    } catch (e) {
        try { probeProp.expression = ""; probe.remove(); } catch (e2) {}
        app.endUndoGroup();
        return err(e.message || e.toString());
    }
    probeProp.expression = "";
    probe.remove();
    // 선택 복원 (probe 추가로 풀린 선택)
    for (var rs = 0; rs < sel.length; rs++) { try { sel[rs].selected = true; } catch (e3) {} }
    app.endUndoGroup();
    return ok({ moved: moved, skipped: skipped, warnings: warns, axis: axis, mode: mode, ref: ref });
}


// ============================================================
//  Cloner — Cinema 4D MoGraph Cloner 를 참고한 라이브 복제 (v1.3: 단일 Pseudo Effect)
//  · 소스 레이어에 "BANG 클로너" 이펙트 하나(jsx/BANG_Cloner.ffx, Pseudo/BANG_Cloner)를 적용한다.
//    항목: 배치 모드 · 복제 개수 · Linear{오프셋} · Grid{열 개수, 간격} ·
//          Radial{반지름, 시작/끝 각도, 반지름 방향 정렬} · 단계 변화{회전/크기/불투명도 증가} ·
//          랜덤{시드, 위치/회전/크기 랜덤}
//  · 클론 = 소스 복제본(shy + lock, 라벨색). Position/Rotation/Scale/Opacity 표현식이
//    소스 이펙트 값 + 자기 인덱스로 배치를 계산 → 이펙트 값을 바꾸면 실시간 반영.
//    소스 자신 = 클론 #0(표현식 없음). 소스를 옮기면 전체가 따라온다.
//  · "복제 개수" 변경은 레이어 수를 바꿔야 하므로 패널의 Cloner 버튼을 다시 누른다(Re-clone).
//  · Bake(래스터라이즈): 클론의 표현식을 현재 값으로 굳히고 shy/lock 해제 → 독립 레이어.
// ============================================================

var CL_FX_NAME  = "BANG 클로너";
var CL_FX_MATCH = "Pseudo/BANG_Cloner";
var CL_LABEL    = 11;   // 클론 라벨색 (Sea Foam)

function _clFindEffect(layer, name) {
    var fx = layer.property("ADBE Effect Parade");
    for (var i = 1; i <= fx.numProperties; i++) if (fx.property(i).name === name) return fx.property(i);
    return null;
}
function _clFindClonerFx(layer) {
    var fx = layer.property("ADBE Effect Parade");
    for (var i = 1; i <= fx.numProperties; i++) if (fx.property(i).matchName === CL_FX_MATCH) return fx.property(i);
    return null;
}
function _clAddControl(layer, matchName, name, value) {
    var ctl = layer.property("ADBE Effect Parade").addProperty(matchName);
    ctl.name = name;
    if (value !== undefined && value !== null) { try { ctl.property(1).setValue(value); } catch (e) {} }
    return ctl;
}

// 소스에 클로너 이펙트 확보. 반환: { fx, created }
function _clEnsureEffect(src, ffxPath) {
    var fx = _clFindClonerFx(src);
    if (fx !== null) return { fx: fx, created: false };
    var before = src.property("ADBE Effect Parade").numProperties;
    var applied = false;
    if (ffxPath) {
        var f = new File(ffxPath);
        if (f.exists) { try { src.applyPreset(f); applied = true; } catch (e1) {} }
    }
    if (!applied) {
        // 이 PC 에 Pseudo 가 등록돼 있으면(개발기) 직접 추가
        try { src.property("ADBE Effect Parade").addProperty(CL_FX_MATCH); applied = true; } catch (e2) {}
    }
    fx = _clFindClonerFx(src);
    if (!applied || fx === null) throw new Error("클로너 이펙트를 적용하지 못했습니다 (BANG_Cloner.ffx 경로 확인: " + ffxPath + ")");
    fx.name = CL_FX_NAME;
    return { fx: fx, created: true };
}

// 클론 판별: Clone Index + Cloner Source(Layer Control) 가 src 를 가리키는 레이어
function _clFindClones(comp, src) {
    var out = [];
    for (var i = 1; i <= comp.numLayers; i++) {
        var ly = comp.layer(i);
        if (ly === src) continue;
        var idx = _clFindEffect(ly, "Clone Index");
        var ref = _clFindEffect(ly, "Cloner Source");
        if (idx === null || ref === null) continue;
        var target = null;
        try { target = ref.property(1).value; } catch (e) {}
        if (target === src.index) out.push(ly);
    }
    return out;
}

// ── 표현식 텍스트 (파라미터는 한글 이름으로 참조) ─────────────
var CL_EXPR_HEAD =
    'var src = effect("Cloner Source")(1);\n' +
    'var i = effect("Clone Index")(1).value;\n' +
    'var FX = src.effect("' + CL_FX_NAME + '");\n' +
    'function C(n){ return FX(n); }\n' +
    'var mode = Math.round(C("배치 모드").value);\n' +
    'var n = Math.max(1, Math.round(C("복제 개수").value));\n' +
    'seedRandom(C("시드").value + i, true);\n';

var CL_EXPR_ANGLE =
    'var a0 = degreesToRadians(C("시작 각도").value), a1 = degreesToRadians(C("끝 각도").value);\n' +
    'var span = a1 - a0;\n' +
    'var full = Math.abs(Math.abs(span) - 2*Math.PI) < 1e-6;\n' +
    'var t = 0; if (full) { t = i / n; } else if (n > 1) { t = i / (n - 1); }\n' +
    'var ang = a0 + span * t;\n';

function _clExprPosition(is3D) {
    return CL_EXPR_HEAD +
    'var base = src.transform.position.value;\n' +
    'if (base.length < 3) base = [base[0], base[1], 0];\n' +
    // 소스 스케일에 배치도 연동: 소스를 키우면 간격·반지름도 같은 비율로 커진다 (전체가 하나처럼 스케일)
    'var ss = src.transform.scale.value; var sf = [ss[0] / 100, ss[1] / 100, (ss.length > 2 ? ss[2] : 100) / 100];\n' +
    'var p = base;\n' +
    'if (mode == 1) {\n' +
    '  var off = C("오프셋").value; p = base + [off[0] * sf[0], off[1] * sf[1], off[2] * sf[2]] * i;\n' +
    '} else if (mode == 2) {\n' +
    '  var cols = Math.max(1, Math.round(C("열 개수").value));\n' +
    '  var sp = C("간격").value;\n' +
    '  p = base + [sp[0] * sf[0] * (i % cols), sp[1] * sf[1] * Math.floor(i / cols), 0];\n' +
    '} else {\n' +
    CL_EXPR_ANGLE +
    '  var r = C("반지름").value;\n' +
    '  var center = base - [Math.cos(a0) * r * sf[0], Math.sin(a0) * r * sf[1], 0];\n' +
    '  p = center + [Math.cos(ang) * r * sf[0], Math.sin(ang) * r * sf[1], 0];\n' +
    '}\n' +
    'var rp = C("위치 랜덤").value;\n' +
    'p = p + [random(-rp[0], rp[0]), random(-rp[1], rp[1]), random(-rp[2], rp[2])];\n' +
    (is3D ? 'p' : '[p[0], p[1]]');
}
function _clExprRotation() {
    return CL_EXPR_HEAD +
    'var rot = src.transform.rotation.value + C("회전 증가").value * i;\n' +
    'if (mode == 3 && C("반지름 방향 정렬").value == 1) {\n' +
    CL_EXPR_ANGLE +
    '  rot += radiansToDegrees(ang - a0);\n' +
    '}\n' +
    'var rr = C("회전 랜덤").value;\n' +
    'rot + random(-rr, rr)';
}
function _clExprScale(is3D) {
    return CL_EXPR_HEAD +
    'var s = src.transform.scale.value;\n' +
    'var f = 1 + (C("크기 증가 %").value / 100) * i;\n' +
    'var rs = C("크기 랜덤 %").value / 100;\n' +
    'f = f * (1 + random(-rs, rs));\n' +
    'if (f < 0) f = 0;\n' +
    (is3D ? '[s[0] * f, s[1] * f, (s.length > 2 ? s[2] : 100) * f]' : '[s[0] * f, s[1] * f]');
}
function _clExprOpacity() {
    return CL_EXPR_HEAD +
    'clamp(src.transform.opacity.value + C("불투명도 증가").value * i, 0, 100)';
}
function _clPad3(n) { return (n < 10 ? "00" : (n < 100 ? "0" : "")) + n; }

// 선택에서 소스 찾기: 소스 자체 또는 그 클론을 선택해도 소스로 귀결
function _clResolveSource(comp) {
    var sel = comp.selectedLayers;
    if (sel.length !== 1) return null;
    var l = sel[0];
    var ref = _clFindEffect(l, "Cloner Source");
    if (ref !== null && _clFindEffect(l, "Clone Index") !== null) {
        try { var idx = ref.property(1).value; if (idx >= 1 && idx <= comp.numLayers) return comp.layer(idx); } catch (e) {}
    }
    return l;
}

var CL_BAKE_PARAM = "래스터라이즈";   // 접두어 매칭 (AE 가 긴 파라미터 이름을 잘라 표시함)
function _clFindParamByPrefix(fx, prefix) {
    for (var i = 1; i <= fx.numProperties; i++) if (fx.property(i).name.indexOf(prefix) === 0) return fx.property(i);
    return null;
}

// 클론 생성/갱신 본체. count 는 소스 포함 총 개수(1 이면 클론 없음)
function _clBuildClones(comp, src, count, is3D) {
    var old = _clFindClones(comp, src);
    for (var o = old.length - 1; o >= 0; o--) { old[o].locked = false; old[o].remove(); }
    var wasLocked = src.locked; src.locked = false;
    var baseName = src.name, made = [], prev = src;
    for (var k = 1; k < count; k++) {
        var cl = src.duplicate();
        cl.moveAfter(prev);
        prev = cl;
        cl.name = baseName + " • " + _clPad3(k);
        cl.selected = false;
        var cfx = _clFindClonerFx(cl);
        if (cfx !== null) cfx.remove();                       // 클로너 이펙트는 소스에만
        var refCtl = _clAddControl(cl, "ADBE Layer Control", "Cloner Source");
        refCtl.property(1).setValue(src.index);
        _clAddControl(cl, "ADBE Slider Control", "Clone Index", k);
        var tg = cl.property("ADBE Transform Group");
        var pos = tg.property("ADBE Position");
        if (pos.dimensionsSeparated) pos.dimensionsSeparated = false;
        pos.expression = _clExprPosition(is3D);
        var rotProp = tg.property("ADBE Rotate Z");
        if (rotProp) rotProp.expression = _clExprRotation();
        tg.property("ADBE Scale").expression = _clExprScale(is3D);
        tg.property("ADBE Opacity").expression = _clExprOpacity();
        cl.label = CL_LABEL;
        cl.shy = true;
        cl.locked = true;
        made.push(cl);
    }
    src.locked = wasLocked;
    if (made.length > 0) comp.hideShyLayers = true;          // 타임라인엔 소스만
    return made;
}

// 패널 진입점 (버튼 하나) — 모든 설정은 소스 레이어의 "BANG 클로너" 이펙트
//   · 이펙트 없음            → jsx/BANG_Cloner.ffx 적용(기본값 5개, Linear) + 복제
//   · 이펙트 있음            → "복제 개수"/"배치 모드" 로 갱신 (개수 1 = 클론 제거)
//   · "래스터라이즈" 체크    → 클론을 독립 레이어로 굳히고 이펙트 제거
//   ffxPath: 확장 폴더의 BANG_Cloner.ffx 절대 경로(패널이 넘김)
function applyCloner(ffxPath) {
    var comp = app.project ? app.project.activeItem : null;
    if (!(comp && comp instanceof CompItem)) return err("활성 컴프가 없습니다.");
    var src = _clResolveSource(comp);
    if (src === null) return err("소스 레이어를 하나만 선택해 주세요.");
    if (src instanceof CameraLayer || src instanceof LightLayer) return err("카메라·라이트는 복제할 수 없습니다.");
    var is3D = false;
    try { is3D = src.threeDLayer === true; } catch (e) {}

    app.beginUndoGroup("BANG Cloner");
    try {
        var got = _clEnsureEffect(src, ffxPath);
        var fx = got.fx;

        // 래스터라이즈 체크 → bake
        var bakeOn = false;
        try { var bp = _clFindParamByPrefix(fx, CL_BAKE_PARAM); if (bp) bakeOn = (bp.value === true || bp.value === 1); } catch (eB) {}
        if (!got.created && bakeOn) {
            var n = _clBakeClones(comp, src);
            app.endUndoGroup();
            return ok({ action: "baked", source: src.name, baked: n });
        }

        var count = Math.round(fx.property("복제 개수").value);
        if (isNaN(count) || count < 1) count = 1;
        if (count > 500) count = 500;
        var made = _clBuildClones(comp, src, count, is3D);
        src.selected = true;
        app.endUndoGroup();
        var action = got.created ? "created" : (count <= 1 ? "removed" : "recloned");
        return ok({ action: action, source: src.name, count: count, clones: made.length, is3D: is3D });
    } catch (eMain) {
        app.endUndoGroup();
        return err(eMain.message || eMain.toString());
    }
}

// ── 네이티브 이펙트 (native/*.aex — BANG Cloner · BANG Stroke) ──
// 선택한 레이어마다 matchName 이펙트를 추가(이미 있으면 그대로). 플러그인 미설치면 {missing:true}
function applyNativeEffect(matchName) {
    var comp = app.project ? app.project.activeItem : null;
    if (!(comp && comp instanceof CompItem)) return err("활성 컴프가 없습니다.");
    var loaded = false;
    for (var i = 0; i < app.effects.length; i++) if (app.effects[i].matchName === matchName) { loaded = true; break; }
    if (!loaded) return ok({ missing: true, effect: matchName });
    var layers = comp.selectedLayers.slice(0);
    if (layers.length === 0) return err("레이어를 선택해 주세요.");
    app.beginUndoGroup(matchName);
    var added = 0, kept = 0;
    try {
        for (var j = 0; j < layers.length; j++) {
            var L = layers[j];
            if (L instanceof CameraLayer || L instanceof LightLayer) continue;
            var parade = L.property("ADBE Effect Parade");
            if (!parade) continue;
            var found = null;
            for (var k = 1; k <= parade.numProperties; k++) if (parade.property(k).matchName === matchName) { found = parade.property(k); break; }
            if (found) { kept++; continue; }
            parade.addProperty(matchName);
            added++;
        }
        app.endUndoGroup();
        return ok({ effect: matchName, added: added, kept: kept });
    } catch (e) {
        app.endUndoGroup();
        return err(e.message || e.toString());
    }
}

// (하위 호환) 이전 진입점
function createCloner(count, modeIndex, ffxPath) { return applyCloner(ffxPath); }

// 패널 자동 갱신용 상태 조회 (가벼움): 선택이 클로너 소스/클론이면 {count, clones, bake}
function clonerPollState() {
    try {
        var comp = app.project ? app.project.activeItem : null;
        if (!(comp && comp instanceof CompItem)) return ok({ active: false });
        var src = _clResolveSource(comp);
        if (src === null) return ok({ active: false });
        var fx = _clFindClonerFx(src);
        if (fx === null) return ok({ active: false });
        var bake = false;
        try { var bp = _clFindParamByPrefix(fx, CL_BAKE_PARAM); if (bp) bake = (bp.value === true || bp.value === 1); } catch (e0) {}
        return ok({ active: true, source: src.name, count: Math.round(fx.property("복제 개수").value),
                    clones: _clFindClones(comp, src).length, bake: bake });
    } catch (e) { return ok({ active: false }); }
}

// 클론 제거(소스 또는 클론 선택) — 이펙트는 남김
function removeClones() {
    var comp = app.project ? app.project.activeItem : null;
    if (!(comp && comp instanceof CompItem)) return err("활성 컴프가 없습니다.");
    var src = _clResolveSource(comp);
    if (src === null) return err("소스 레이어를 하나만 선택해 주세요.");
    var old = _clFindClones(comp, src);
    if (old.length === 0) return err("이 레이어의 클론이 없습니다.");
    app.beginUndoGroup("BANG Cloner Remove");
    for (var o = old.length - 1; o >= 0; o--) { old[o].locked = false; old[o].remove(); }
    src.selected = true;
    app.endUndoGroup();
    return ok({ removed: old.length });
}

// Bake(래스터라이즈): 클론을 독립 레이어로 굳힘 — 표현식 → 현재 시간 값, shy/lock 해제, 참조 이펙트 제거, 소스의 클로너 이펙트 제거
function _clBakeClones(comp, src) {
    var clones = _clFindClones(comp, src);
    if (clones.length === 0) throw new Error("이 레이어의 클론이 없습니다.");
    var t = comp.time;
    var names = ["ADBE Position", "ADBE Rotate Z", "ADBE Scale", "ADBE Opacity"];
    for (var c = 0; c < clones.length; c++) {
        var cl = clones[c];
        cl.locked = false;
        var tg = cl.property("ADBE Transform Group");
        for (var n = 0; n < names.length; n++) {
            var p = tg.property(names[n]);
            if (!p || !p.expressionEnabled) continue;
            var v = p.valueAtTime(t, false);
            p.expression = "";
            if (p.numKeys > 0) p.setValueAtTime(t, v); else p.setValue(v);
        }
        var e1 = _clFindEffect(cl, "Clone Index");  if (e1) e1.remove();
        var e2 = _clFindEffect(cl, "Cloner Source"); if (e2) e2.remove();
        cl.shy = false;
        cl.label = src.label;
        cl.selected = true;
    }
    var fx = _clFindClonerFx(src);
    if (fx !== null) fx.remove();
    src.selected = true;
    return clones.length;
}
function bakeClones() {
    var comp = app.project ? app.project.activeItem : null;
    if (!(comp && comp instanceof CompItem)) return err("활성 컴프가 없습니다.");
    var src = _clResolveSource(comp);
    if (src === null) return err("소스 레이어를 하나만 선택해 주세요.");
    app.beginUndoGroup("BANG Cloner Bake");
    try { var n = _clBakeClones(comp, src); app.endUndoGroup(); return ok({ baked: n }); }
    catch (e) { app.endUndoGroup(); return err(e.message || e.toString()); }
}

// ============================================================
//  Bento Grid — BentoGrid.jsx v1.3.0 (방명환) 이식
//  · 원본 엔진(타일 크기 배정·빔/그리디 패킹·크롭 마스크·마스크 정리)을 그대로 가져오고,
//    ScriptUI 팔레트 대신 패널이 넘긴 설정 객체를 원본 readUISettings 가 읽는 형태(ui 셈)로 연결한다.
//  · 원본의 alert()는 클로저 안에서 메시지 수집 함수로 대체 → 패널 상태바로 전달.
// ============================================================
var BANG_Bento = (function () {
    var __messages = [];
    function alert(message) { __messages.push(String(message)); }
    function confirm() { return true; }

    var SCRIPT_NAME = "Bento Grid";
    var VERSION = "1.3.0";
    var SETTINGS_SECTION = "BentoGridPanel_v1";
    var CROP_MASK_NAME = "__BENTO_GRID_CROP__";
    var EPSILON = 0.0001;
    var MAX_COLUMNS = 500;
    var MAX_EXPRESSION_LENGTH = 256;
    var MAX_EXPRESSION_DEPTH = 32;
    var randomCounter = 0;

    function isFiniteNumber(value) {
        return typeof value === "number" && !isNaN(value) && isFinite(value);
    }

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value));
    }

    function trimText(value) {
        return String(value).replace(/^\s+|\s+$/g, "");
    }

    function evaluateMathExpression(text, label) {
        var source = trimText(text);
        var index = 0;
        var length = source.length;
        var depth = 0;

        function fail() {
            throw new Error(label +
                " must be a valid calculation using numbers, +, -, *, /, and parentheses.");
        }

        function skipWhitespace() {
            while (index < length && /\s/.test(source.charAt(index))) {
                index++;
            }
        }

        function checked(value) {
            if (!isFiniteNumber(value)) {
                throw new Error(label + " calculation is too large or divides by zero.");
            }
            return value;
        }

        function parseNumber() {
            var start;
            var sawDigit = false;
            var character;
            var value;

            skipWhitespace();
            start = index;
            while (index < length) {
                character = source.charAt(index);
                if (character >= "0" && character <= "9") {
                    sawDigit = true;
                    index++;
                } else {
                    break;
                }
            }
            if (source.charAt(index) === ".") {
                index++;
                while (index < length) {
                    character = source.charAt(index);
                    if (character >= "0" && character <= "9") {
                        sawDigit = true;
                        index++;
                    } else {
                        break;
                    }
                }
            }
            if (!sawDigit) {
                fail();
            }
            value = Number(source.substring(start, index));
            return checked(value);
        }

        function parsePrimary() {
            var value;

            skipWhitespace();
            if (source.charAt(index) === "(") {
                index++;
                depth++;
                if (depth > MAX_EXPRESSION_DEPTH) {
                    throw new Error(label + " calculation has too many nested parentheses.");
                }
                value = parseExpression();
                skipWhitespace();
                if (source.charAt(index) !== ")") {
                    fail();
                }
                index++;
                depth--;
                return value;
            }
            return parseNumber();
        }

        function parseUnary() {
            var sign = 1;
            var character;

            skipWhitespace();
            character = source.charAt(index);
            while (character === "+" || character === "-") {
                if (character === "-") {
                    sign = -sign;
                }
                index++;
                skipWhitespace();
                character = source.charAt(index);
            }
            return checked(sign * parsePrimary());
        }

        function parseTerm() {
            var value = parseUnary();
            var operator;
            var right;

            while (true) {
                skipWhitespace();
                operator = source.charAt(index);
                if (operator !== "*" && operator !== "/") {
                    break;
                }
                index++;
                right = parseUnary();
                if (operator === "/" && right === 0) {
                    throw new Error(label + " calculation cannot divide by zero.");
                }
                value = checked(operator === "*" ? value * right : value / right);
            }
            return value;
        }

        function parseExpression() {
            var value = parseTerm();
            var operator;
            var right;

            while (true) {
                skipWhitespace();
                operator = source.charAt(index);
                if (operator !== "+" && operator !== "-") {
                    break;
                }
                index++;
                right = parseTerm();
                value = checked(operator === "+" ? value + right : value - right);
            }
            return value;
        }

        var result;

        if (source.length === 0) {
            fail();
        }
        if (source.length > MAX_EXPRESSION_LENGTH) {
            throw new Error(label + " calculation is too long.");
        }
        result = parseExpression();
        skipWhitespace();
        if (index !== length) {
            fail();
        }
        return checked(result);
    }

    function parsePositiveNumber(text, label, allowZero) {
        var value = evaluateMathExpression(text, label);
        var minimum = allowZero ? 0 : EPSILON;

        if (value < minimum) {
            throw new Error(label + (allowZero ? " must be 0 or greater." : " must be greater than 0."));
        }
        return value;
    }

    function getSavedSetting(key, fallback) {
        try {
            if (app.settings.haveSetting(SETTINGS_SECTION, key)) {
                return app.settings.getSetting(SETTINGS_SECTION, key);
            }
        } catch (ignore) {
        }
        return fallback;
    }

    function saveSetting(key, value) {
        try {
            app.settings.saveSetting(SETTINGS_SECTION, key, String(value));
        } catch (ignore) {
        }
    }

    function saveUISettings(settings) {
        saveSetting("unit", settings.unitExpression);
        saveSetting("gap", settings.gapExpression);
        saveSetting("width", settings.layoutWidthExpression);
        saveSetting("fit", settings.fitMode);
        saveSetting("variety", settings.tileVariety);
        saveSetting("packingStyle", settings.packingStyle);
        saveSetting("mixOrientations", settings.mixOrientations ? "1" : "0");
        saveSetting("crop", settings.cropCover ? "1" : "0");
        saveSetting("center", settings.centerLayout ? "1" : "0");
    }

    function RNG(seed) {
        this.state = Math.floor(Math.abs(seed)) % 2147483647;
        if (this.state <= 0) {
            this.state += 2147483646;
        }
    }

    RNG.prototype.next = function () {
        this.state = (this.state * 16807) % 2147483647;
        return (this.state - 1) / 2147483646;
    };

    function shuffledCopy(values, rng) {
        var copy = values.slice(0);
        var i;
        var j;
        var temp;

        for (i = copy.length - 1; i > 0; i--) {
            j = Math.floor(rng.next() * (i + 1));
            temp = copy[i];
            copy[i] = copy[j];
            copy[j] = temp;
        }
        return copy;
    }

    function normalizedAngle(value) {
        var angle = value % 360;
        if (angle < 0) {
            angle += 360;
        }
        return angle;
    }

    function propertyHasExpression(property) {
        try {
            return property.expressionEnabled === true;
        } catch (ignore) {
        }
        return false;
    }

    function propertyIsStatic(property) {
        if (!property) {
            return false;
        }
        try {
            if (property.numKeys > 0) {
                return false;
            }
        } catch (ignoreKeys) {
        }
        return !propertyHasExpression(property);
    }

    function getStaticPositionProperties(transformGroup) {
        var leader = transformGroup.property("ADBE Position");
        var separated = false;
        var xProperty;
        var yProperty;

        if (!leader) {
            return null;
        }

        try {
            separated = leader.dimensionsSeparated === true;
        } catch (ignore) {
            separated = false;
        }

        if (!separated) {
            if (!propertyIsStatic(leader)) {
                return null;
            }
            return {
                separated: false,
                leader: leader
            };
        }

        xProperty = transformGroup.property("ADBE Position_0");
        yProperty = transformGroup.property("ADBE Position_1");
        if (!propertyIsStatic(xProperty) || !propertyIsStatic(yProperty)) {
            return null;
        }

        return {
            separated: true,
            xProperty: xProperty,
            yProperty: yProperty
        };
    }

    function setStaticPosition(positionInfo, x, y) {
        var current;

        if (positionInfo.separated) {
            positionInfo.xProperty.setValue(x);
            positionInfo.yProperty.setValue(y);
            return;
        }

        current = positionInfo.leader.value;
        if (current && current.length > 2) {
            positionInfo.leader.setValue([x, y, current[2]]);
        } else {
            positionInfo.leader.setValue([x, y]);
        }
    }

    function layerLabel(layer) {
        var name = "Layer";
        try {
            name = layer.name;
        } catch (ignore) {
        }
        return "#" + layer.index + " " + name;
    }

    function inspectLayer(layer, comp, order) {
        var source;
        var isFootage = false;
        var isPrecomp = false;
        var transformGroup;
        var anchorProperty;
        var scaleProperty;
        var rotationProperty;
        var positionInfo;
        var sourceWidth;
        var sourceHeight;
        var sourcePAR = 1;
        var compPAR = 1;
        var parX;
        var angle;

        try {
            if (!(layer instanceof AVLayer) || !layer.hasVideo || layer.nullLayer || layer.adjustmentLayer) {
                return {reason: "not a visual AV layer"};
            }
        } catch (typeError) {
            return {reason: "unsupported layer type"};
        }

        if (layer.locked) {
            return {reason: "locked"};
        }
        if (layer.threeDLayer) {
            return {reason: "3D layer"};
        }
        if (layer.parent !== null) {
            return {reason: "parented layer"};
        }

        try {
            if (layer.hasTrackMatte || layer.isTrackMatte) {
                return {reason: "track matte relationship"};
            }
        } catch (ignoreMatte) {
        }

        try {
            source = layer.source;
        } catch (sourceError) {
            source = null;
        }
        if (!source) {
            return {reason: "no measurable source"};
        }

        try {
            isFootage = source instanceof FootageItem;
        } catch (ignoreFootageType) {
        }
        try {
            isPrecomp = source instanceof CompItem;
        } catch (ignoreCompType) {
        }
        if (!isFootage && !isPrecomp) {
            return {reason: "unsupported source type"};
        }

        if (isFootage) {
            try {
                if (source.footageMissing) {
                    return {reason: "missing footage"};
                }
            } catch (ignoreMissing) {
            }
            try {
                if (source.file === null) {
                    return {reason: "solid or placeholder source"};
                }
            } catch (fileError) {
                return {reason: "non-file footage source"};
            }
        }

        try {
            if (layer.collapseTransformation) {
                return {reason: "continuous rasterization/collapse transformations"};
            }
        } catch (ignoreCollapse) {
        }

        transformGroup = layer.property("ADBE Transform Group");
        if (!transformGroup) {
            return {reason: "missing Transform group"};
        }

        anchorProperty = transformGroup.property("ADBE Anchor Point");
        scaleProperty = transformGroup.property("ADBE Scale");
        rotationProperty = transformGroup.property("ADBE Rotate Z");
        positionInfo = getStaticPositionProperties(transformGroup);

        if (!propertyIsStatic(anchorProperty) || !propertyIsStatic(scaleProperty) ||
                !propertyIsStatic(rotationProperty) || !positionInfo) {
            return {reason: "animated or expression-driven Transform"};
        }

        angle = normalizedAngle(Number(rotationProperty.value));
        if (!isFiniteNumber(angle)) {
            return {reason: "invalid Rotation value"};
        }
        if (angle > EPSILON && angle < 360 - EPSILON) {
            return {reason: "rotated layer"};
        }

        try {
            sourceWidth = Number(layer.width);
            sourceHeight = Number(layer.height);
        } catch (dimensionError) {
            return {reason: "unreadable source dimensions"};
        }
        if (!isFiniteNumber(sourceWidth) || !isFiniteNumber(sourceHeight) ||
                sourceWidth <= 0 || sourceHeight <= 0) {
            return {reason: "zero or invalid source dimensions"};
        }

        try {
            sourcePAR = Number(source.pixelAspect);
        } catch (ignoreSourcePAR) {
            sourcePAR = 1;
        }
        try {
            compPAR = Number(comp.pixelAspect);
        } catch (ignoreCompPAR) {
            compPAR = 1;
        }
        if (!isFiniteNumber(sourcePAR) || sourcePAR <= 0) {
            sourcePAR = 1;
        }
        if (!isFiniteNumber(compPAR) || compPAR <= 0) {
            compPAR = 1;
        }
        parX = sourcePAR / compPAR;

        return {
            item: {
                id: order,
                order: order,
                layer: layer,
                transformGroup: transformGroup,
                anchorProperty: anchorProperty,
                scaleProperty: scaleProperty,
                positionInfo: positionInfo,
                sourceWidth: sourceWidth,
                sourceHeight: sourceHeight,
                sourcePAR: sourcePAR,
                parX: parX,
                displayWidth: sourceWidth * parX,
                displayHeight: sourceHeight,
                aspect: (sourceWidth * parX) / sourceHeight,
                nativeArea: sourceWidth * sourceHeight * parX,
                tileW: 1,
                tileH: 1,
                tileArea: 1
            }
        };
    }

    function tilePixelWidth(span, unit, gap) {
        return span * unit + (span - 1) * gap;
    }

    function tileAspect(widthInCells, heightInCells, unit, gap) {
        return tilePixelWidth(widthInCells, unit, gap) /
            tilePixelWidth(heightInCells, unit, gap);
    }

    function aspectCost(sourceAspect, widthInCells, heightInCells, unit, gap) {
        return Math.abs(Math.log(sourceAspect /
            tileAspect(widthInCells, heightInCells, unit, gap)));
    }

    function chooseAspectTile(item, columns, unit, gap) {
        var candidates = [
            {w: 1, h: 1}
        ];
        var best;
        var bestCost;
        var candidate;
        var cost;
        var i;

        if (columns >= 2) {
            candidates.push({w: 2, h: 1});
        }
        candidates.push({w: 1, h: 2});
        if (columns >= 3) {
            candidates.push({w: 3, h: 1});
        }
        candidates.push({w: 1, h: 3});

        best = candidates[0];
        bestCost = aspectCost(item.aspect, best.w, best.h, unit, gap);
        for (i = 1; i < candidates.length; i++) {
            candidate = candidates[i];
            cost = aspectCost(item.aspect, candidate.w, candidate.h, unit, gap);
            if (cost < bestCost - EPSILON ||
                    (Math.abs(cost - bestCost) <= EPSILON &&
                    candidate.w * candidate.h < best.w * best.h)) {
                best = candidate;
                bestCost = cost;
            }
        }

        item.tileW = best.w;
        item.tileH = best.h;
        item.tileArea = best.w * best.h;
        item.aspectFitCost = bestCost;
    }

    function assignBalancedTileSizes(items, columns, unit, gap, randomize, rng,
            packingStyle) {
        var squareCandidates = [];
        var stochastic = randomize || packingStyle !== "Compact";
        var featureCount;
        var maxFeatureCount;
        var item;
        var squareCost;
        var i;

        for (i = 0; i < items.length; i++) {
            item = items[i];
            chooseAspectTile(item, columns, unit, gap);
            squareCost = aspectCost(item.aspect, 1, 1, unit, gap);
            if (item.tileW === 1 && item.tileH === 1 && columns >= 2 &&
                    items.length >= 4 && squareCost <= Math.log(1.5)) {
                squareCandidates.push(item);
            }
        }

        featureCount = items.length >= 4 ? Math.max(1, Math.floor((items.length + 2) / 6)) : 0;
        maxFeatureCount = Math.max(1, Math.floor((items.length - 1) / 3));
        featureCount = Math.min(featureCount, maxFeatureCount, squareCandidates.length);

        for (i = 0; i < squareCandidates.length; i++) {
            squareCandidates[i]._featureRank = Math.log(Math.max(1, squareCandidates[i].nativeArea));
            if (stochastic) {
                squareCandidates[i]._featureRank += (rng.next() - 0.5) * 1.25;
            }
        }
        squareCandidates.sort(function (a, b) {
            if (Math.abs(b._featureRank - a._featureRank) > EPSILON) {
                return b._featureRank - a._featureRank;
            }
            return a.order - b.order;
        });

        for (i = 0; i < featureCount; i++) {
            squareCandidates[i].tileW = 2;
            squareCandidates[i].tileH = 2;
            squareCandidates[i].tileArea = 4;
            squareCandidates[i].aspectFitCost = aspectCost(
                squareCandidates[i].aspect, 2, 2, unit, gap
            );
        }
    }

    function makeLargeTileCandidates(item, columns, unit, gap, profile, stochastic, rng) {
        var candidates = [];
        var widthInCells;
        var heightInCells;
        var area;
        var cost;
        var score;

        for (heightInCells = 1; heightInCells <= profile.maxSpan; heightInCells++) {
            for (widthInCells = 1; widthInCells <= profile.maxSpan; widthInCells++) {
                area = widthInCells * heightInCells;
                if (widthInCells > columns || area <= item.tileArea) {
                    continue;
                }
                cost = aspectCost(
                    item.aspect, widthInCells, heightInCells, unit, gap
                );
                if (cost > item.aspectFitCost + profile.aspectTolerance + EPSILON) {
                    continue;
                }
                score = cost - profile.areaBias * Math.log(area);
                if (stochastic) {
                    score += (rng.next() - 0.5) * profile.shapeJitter;
                }
                candidates.push({
                    w: widthInCells,
                    h: heightInCells,
                    area: area,
                    addedArea: area - item.tileArea,
                    cost: cost,
                    score: score
                });
            }
        }

        candidates.sort(function (a, b) {
            if (Math.abs(a.score - b.score) > EPSILON) {
                return a.score - b.score;
            }
            if (a.area !== b.area) {
                return b.area - a.area;
            }
            if (a.w !== b.w) {
                return b.w - a.w;
            }
            return a.h - b.h;
        });
        return candidates;
    }

    function chooseProportionalTierChoice(item, remainingBudget, profile,
            packingStyle, rng) {
        var randomValue;
        var targetMultiplier;
        var maximumMultiplier;
        var choice;
        var multiplier;
        var i;

        if (packingStyle === "Compact") {
            return null;
        }

        maximumMultiplier = Math.floor(profile.maxSpan /
            Math.max(item.baseTileW, item.baseTileH));
        if (maximumMultiplier <= 1) {
            return null;
        }
        randomValue = rng.next();
        if (maximumMultiplier === 2) {
            targetMultiplier = 2;
        } else if (maximumMultiplier === 3) {
            targetMultiplier = randomValue <
                (packingStyle === "Loose Mosaic" ? 0.55 : 0.72) ? 2 : 3;
        } else if (packingStyle === "Loose Mosaic") {
            targetMultiplier = randomValue < 0.38 ? 2 :
                (randomValue < 0.70 ? 3 : 4);
        } else {
            targetMultiplier = randomValue < 0.55 ? 2 :
                (randomValue < 0.83 ? 3 : 4);
        }
        targetMultiplier = Math.min(targetMultiplier, maximumMultiplier);

        for (multiplier = targetMultiplier; multiplier >= 2; multiplier--) {
            for (i = 0; i < item._largeTileChoices.length; i++) {
                choice = item._largeTileChoices[i];
                if (choice.w === item.baseTileW * multiplier &&
                        choice.h === item.baseTileH * multiplier &&
                        choice.addedArea <= remainingBudget) {
                    return choice;
                }
            }
        }
        return null;
    }

    function assignLargeTileSizes(items, columns, unit, gap, randomize, rng,
            variety, packingStyle) {
        var stochastic = randomize || packingStyle !== "Compact";
        var profile = variety === "Wild" ? {
            maxSpan: 4,
            aspectTolerance: 0.55,
            areaBias: 0.08,
            shapeJitter: 0.60,
            rankJitter: 1.50,
            budgetRatio: 1.25,
            minimumBudget: 15,
            divisor: 3,
            countOffset: 2
        } : {
            maxSpan: 3,
            aspectTolerance: 0.32,
            areaBias: 0.05,
            shapeJitter: 0.24,
            rankJitter: 0.80,
            budgetRatio: 0.75,
            minimumBudget: 8,
            divisor: 4,
            countOffset: 1
        };
        var featureCandidates = [];
        var featureCount;
        var maxFeatureCount;
        var baseTotalArea = 0;
        var addedAreaBudget;
        var usedAddedArea = 0;
        var appliedCount = 0;
        var item;
        var choices;
        var choice;
        var i;
        var j;

        if (packingStyle === "Loose Mosaic") {
            profile.shapeJitter *= 1.35;
            profile.rankJitter *= 1.30;
            profile.budgetRatio *= 1.20;
        }

        for (i = 0; i < items.length; i++) {
            item = items[i];
            chooseAspectTile(item, columns, unit, gap);
            item.baseTileW = item.tileW;
            item.baseTileH = item.tileH;
            baseTotalArea += item.tileArea;
            choices = makeLargeTileCandidates(
                item, columns, unit, gap, profile, stochastic, rng
            );
            if (choices.length > 0) {
                item._largeTileChoices = choices;
                item._featureRank = Math.log(Math.max(1, item.nativeArea)) -
                    1.5 * choices[0].cost;
                if (stochastic) {
                    item._featureRank += (rng.next() - 0.5) * profile.rankJitter;
                }
                featureCandidates.push(item);
            }
        }

        featureCount = items.length >= 3 ?
            Math.max(1, Math.floor((items.length + profile.countOffset) /
                profile.divisor)) : 0;
        maxFeatureCount = Math.max(0, items.length - 2);
        if (packingStyle === "Loose Mosaic" && featureCount > 0) {
            featureCount = Math.ceil(featureCount * 1.35);
        }
        featureCount = Math.min(featureCount, maxFeatureCount, featureCandidates.length);
        addedAreaBudget = Math.max(
            profile.minimumBudget,
            Math.floor(baseTotalArea * profile.budgetRatio)
        );

        featureCandidates.sort(function (a, b) {
            if (Math.abs(b._featureRank - a._featureRank) > EPSILON) {
                return b._featureRank - a._featureRank;
            }
            return a.order - b.order;
        });

        for (i = 0; i < featureCandidates.length && appliedCount < featureCount; i++) {
            item = featureCandidates[i];
            choice = chooseProportionalTierChoice(
                item, addedAreaBudget - usedAddedArea, profile, packingStyle, rng
            );
            if (!choice) {
                for (j = 0; j < item._largeTileChoices.length; j++) {
                    if (usedAddedArea + item._largeTileChoices[j].addedArea <=
                            addedAreaBudget) {
                        choice = item._largeTileChoices[j];
                        break;
                    }
                }
            }
            if (!choice) {
                continue;
            }
            item.tileW = choice.w;
            item.tileH = choice.h;
            item.tileArea = choice.area;
            item.aspectFitCost = choice.cost;
            usedAddedArea += choice.addedArea;
            appliedCount++;
        }
    }

    function mixTileOrientations(items, columns, unit, gap, rng, packingStyle,
            enabled) {
        var candidates = [];
        var swapRatio;
        var minimumVisible;
        var swapCount;
        var visibleFraction;
        var item;
        var oldWidth;
        var i;

        if (!enabled || packingStyle === "Compact") {
            return 0;
        }

        swapRatio = packingStyle === "Loose Mosaic" ? 0.26 : 0.14;
        minimumVisible = packingStyle === "Loose Mosaic" ? 0.16 : 0.23;
        for (i = 0; i < items.length; i++) {
            item = items[i];
            if (item.tileW === item.tileH || item.tileH > columns) {
                continue;
            }
            visibleFraction = Math.exp(-aspectCost(
                item.aspect, item.tileH, item.tileW, unit, gap
            ));
            if (visibleFraction + EPSILON < minimumVisible) {
                continue;
            }
            item._orientationRank = rng.next() +
                (Math.max(item.tileW, item.tileH) > 2 ? 0.20 : 0);
            candidates.push(item);
        }

        candidates.sort(function (a, b) {
            if (Math.abs(a._orientationRank - b._orientationRank) > EPSILON) {
                return a._orientationRank - b._orientationRank;
            }
            return a.order - b.order;
        });
        swapCount = Math.floor(candidates.length * swapRatio);
        if (swapCount === 0 && candidates.length >= 4) {
            swapCount = 1;
        }

        for (i = 0; i < swapCount; i++) {
            item = candidates[i];
            oldWidth = item.tileW;
            item.tileW = item.tileH;
            item.tileH = oldWidth;
            item.aspectFitCost = aspectCost(
                item.aspect, item.tileW, item.tileH, unit, gap
            );
            item.orientationMixed = true;
        }
        return swapCount;
    }

    function assignTileSizes(items, columns, unit, gap, randomize, rng, variety,
            packingStyle, mixOrientations) {
        if (variety === "Bold" || variety === "Wild") {
            assignLargeTileSizes(
                items, columns, unit, gap, randomize, rng, variety, packingStyle
            );
        } else {
            assignBalancedTileSizes(
                items, columns, unit, gap, randomize, rng, packingStyle
            );
        }
        return mixTileOrientations(
            items, columns, unit, gap, rng, packingStyle, mixOrientations
        );
    }

    function makeGridState(columns) {
        var heights = [];
        var counts = [];
        var i;

        for (i = 0; i < columns; i++) {
            heights[i] = 0;
            counts[i] = 0;
        }
        return {
            columns: columns,
            grid: [],
            colHeights: heights,
            colCounts: counts,
            usedRows: 0,
            usedCells: 0,
            buried: 0,
            roughness: 0,
            placements: []
        };
    }

    function isOccupied(state, x, y) {
        return state.grid[y] && state.grid[y][x] === true;
    }

    function canPlace(state, tile, x, y) {
        var xx;
        var yy;

        if (x < 0 || y < 0 || x + tile.tileW > state.columns) {
            return false;
        }
        for (yy = y; yy < y + tile.tileH; yy++) {
            for (xx = x; xx < x + tile.tileW; xx++) {
                if (isOccupied(state, xx, yy)) {
                    return false;
                }
            }
        }
        return true;
    }

    function countPlacementContact(state, tile, x, y) {
        var contact = 0;
        var xx;
        var yy;

        for (xx = x; xx < x + tile.tileW; xx++) {
            if (y === 0 || isOccupied(state, xx, y - 1)) {
                contact++;
            }
            if (isOccupied(state, xx, y + tile.tileH)) {
                contact++;
            }
        }
        for (yy = y; yy < y + tile.tileH; yy++) {
            if (x === 0 || isOccupied(state, x - 1, yy)) {
                contact++;
            }
            if (x + tile.tileW === state.columns ||
                    isOccupied(state, x + tile.tileW, yy)) {
                contact++;
            }
        }
        return contact;
    }

    function candidateMetrics(state, tile, x, y, randomize, rng, packingStyle) {
        var rows = state.usedRows;
        var buried = state.buried;
        var roughness = state.roughness;
        var end = x + tile.tileW - 1;
        var newTop = y + tile.tileH;
        var oldHeight;
        var newHeight;
        var oldLeft;
        var oldRight;
        var newLeft;
        var newRight;
        var firstBoundary;
        var lastBoundary;
        var i;

        for (i = x; i <= end; i++) {
            oldHeight = state.colHeights[i];
            newHeight = Math.max(oldHeight, newTop);
            buried += (newHeight - (state.colCounts[i] + tile.tileH)) -
                (oldHeight - state.colCounts[i]);
            rows = Math.max(rows, newHeight);
        }

        firstBoundary = Math.max(0, x - 1);
        lastBoundary = Math.min(state.columns - 2, end);
        for (i = firstBoundary; i <= lastBoundary; i++) {
            oldLeft = state.colHeights[i];
            oldRight = state.colHeights[i + 1];
            newLeft = i >= x && i <= end ? Math.max(oldLeft, newTop) : oldLeft;
            newRight = i + 1 >= x && i + 1 <= end ?
                Math.max(oldRight, newTop) : oldRight;
            roughness += Math.abs(newLeft - newRight) - Math.abs(oldLeft - oldRight);
        }

        return {
            x: x,
            y: y,
            rows: rows,
            buried: buried,
            roughness: roughness,
            contact: countPlacementContact(state, tile, x, y),
            randomTie: randomize || packingStyle !== "Compact" ? rng.next() : 0
        };
    }

    function candidateIsBetter(candidate, best, randomize, packingStyle) {
        var candidateScore;
        var bestScore;

        if (!best) {
            return true;
        }
        if (packingStyle === "Loose Mosaic") {
            candidateScore = candidate.rows * 1.6 + candidate.buried * 4 +
                candidate.roughness * 0.02 - candidate.contact * 0.05 +
                candidate.randomTie * 2.2;
            bestScore = best.rows * 1.6 + best.buried * 4 +
                best.roughness * 0.02 - best.contact * 0.05 +
                best.randomTie * 2.2;
            if (Math.abs(candidateScore - bestScore) > EPSILON) {
                return candidateScore < bestScore;
            }
            if (candidate.rows !== best.rows) {
                return candidate.rows < best.rows;
            }
            if (candidate.buried !== best.buried) {
                return candidate.buried < best.buried;
            }
            if (candidate.y !== best.y) {
                return candidate.y < best.y;
            }
            return candidate.x < best.x;
        }
        if (candidate.rows !== best.rows) {
            return candidate.rows < best.rows;
        }
        if (candidate.buried !== best.buried) {
            return candidate.buried < best.buried;
        }
        if (packingStyle === "Interlocking" &&
                Math.abs(candidate.randomTie - best.randomTie) > EPSILON) {
            return candidate.randomTie < best.randomTie;
        }
        if (candidate.roughness !== best.roughness) {
            return candidate.roughness < best.roughness;
        }
        if (candidate.contact !== best.contact) {
            return candidate.contact > best.contact;
        }
        if (randomize && Math.abs(candidate.randomTie - best.randomTie) > EPSILON) {
            return candidate.randomTie < best.randomTie;
        }
        if (candidate.y !== best.y) {
            return candidate.y < best.y;
        }
        return candidate.x < best.x;
    }

    function findBestPlacement(state, tile, randomize, rng, packingStyle) {
        var best = null;
        var candidate;
        var x;
        var y;

        for (y = 0; y <= state.usedRows; y++) {
            for (x = 0; x <= state.columns - tile.tileW; x++) {
                if (canPlace(state, tile, x, y)) {
                    candidate = candidateMetrics(
                        state, tile, x, y, randomize, rng, packingStyle
                    );
                    if (candidateIsBetter(
                            candidate, best, randomize, packingStyle)) {
                        best = candidate;
                    }
                }
            }
        }
        return best;
    }

    function occupy(state, tile, x, y, metrics) {
        var xx;
        var yy;

        for (yy = y; yy < y + tile.tileH; yy++) {
            if (!state.grid[yy]) {
                state.grid[yy] = [];
            }
            for (xx = x; xx < x + tile.tileW; xx++) {
                state.grid[yy][xx] = true;
            }
        }
        for (xx = x; xx < x + tile.tileW; xx++) {
            state.colHeights[xx] = Math.max(state.colHeights[xx], y + tile.tileH);
            state.colCounts[xx] += tile.tileH;
        }
        state.usedRows = Math.max(state.usedRows, y + tile.tileH);
        state.usedCells += tile.tileArea;
        if (metrics) {
            state.buried = metrics.buried;
            state.roughness = metrics.roughness;
        }
        state.placements.push({item: tile, x: x, y: y});
    }

    function spreadShuffledBuckets(buckets, rng) {
        var combined = [];
        var bucket;
        var item;
        var b;
        var i;

        for (b = 0; b < buckets.length; b++) {
            bucket = shuffledCopy(buckets[b], rng);
            for (i = 0; i < bucket.length; i++) {
                item = bucket[i];
                item._bucketSpread = (i + rng.next() * 0.75) /
                    Math.max(1, bucket.length);
                item._bucketTie = rng.next();
                combined.push(item);
            }
        }
        combined.sort(function (a, b) {
            if (Math.abs(a._bucketSpread - b._bucketSpread) > EPSILON) {
                return a._bucketSpread - b._bucketSpread;
            }
            if (Math.abs(a._bucketTie - b._bucketTie) > EPSILON) {
                return a._bucketTie - b._bucketTie;
            }
            return a.order - b.order;
        });
        return combined;
    }

    function makeAttemptOrder(items, attempt, randomize, rng, packingStyle) {
        var ordered = items.slice(0);
        var buckets;
        var randomValue;
        var i;

        for (i = 0; i < ordered.length; i++) {
            ordered[i]._packRandom = rng.next();
        }

        if (packingStyle !== "Compact") {
            if (attempt === 1 || attempt === 5) {
                return shuffledCopy(ordered, rng);
            }
            if (attempt === 2) {
                buckets = [[], [], []];
                for (i = 0; i < ordered.length; i++) {
                    if (ordered[i].tileW > ordered[i].tileH) {
                        buckets[0].push(ordered[i]);
                    } else if (ordered[i].tileW < ordered[i].tileH) {
                        buckets[1].push(ordered[i]);
                    } else {
                        buckets[2].push(ordered[i]);
                    }
                }
                return spreadShuffledBuckets(buckets, rng);
            }
            if (attempt === 3 ||
                    (packingStyle === "Loose Mosaic" && attempt === 6)) {
                buckets = [[], [], []];
                for (i = 0; i < ordered.length; i++) {
                    if (ordered[i].tileArea <= 2) {
                        buckets[0].push(ordered[i]);
                    } else if (ordered[i].tileArea <= 6) {
                        buckets[1].push(ordered[i]);
                    } else {
                        buckets[2].push(ordered[i]);
                    }
                }
                return spreadShuffledBuckets(buckets, rng);
            }
            if (attempt >= 4) {
                for (i = 0; i < ordered.length; i++) {
                    randomValue = clamp(rng.next(), 0.000001, 0.999999);
                    ordered[i]._organicPriority =
                        (packingStyle === "Loose Mosaic" ? 0.12 : 0.55) *
                        Math.log(Math.max(1, ordered[i].tileArea)) -
                        Math.log(-Math.log(randomValue));
                }
                ordered.sort(function (a, b) {
                    if (Math.abs(b._organicPriority - a._organicPriority) > EPSILON) {
                        return b._organicPriority - a._organicPriority;
                    }
                    return a.order - b.order;
                });
                return ordered;
            }
        }

        if (attempt % 7 === 6) {
            return shuffledCopy(ordered, rng);
        }

        ordered.sort(function (a, b) {
            var difference;

            if (attempt % 5 === 1) {
                difference = b.tileArea - a.tileArea;
                if (difference !== 0) {
                    return difference;
                }
                difference = b.tileW - a.tileW;
                if (difference !== 0) {
                    return difference;
                }
                difference = b.tileH - a.tileH;
                if (difference !== 0) {
                    return difference;
                }
            } else if (attempt % 5 === 2) {
                difference = b.tileH - a.tileH;
                if (difference !== 0) {
                    return difference;
                }
                difference = b.tileArea - a.tileArea;
                if (difference !== 0) {
                    return difference;
                }
            } else if (attempt % 5 === 3) {
                difference = b.tileW - a.tileW;
                if (difference !== 0) {
                    return difference;
                }
                difference = b.tileArea - a.tileArea;
                if (difference !== 0) {
                    return difference;
                }
            } else {
                difference = b.tileArea - a.tileArea;
                if (difference !== 0) {
                    return difference;
                }
                difference = b.tileH - a.tileH;
                if (difference !== 0) {
                    return difference;
                }
                difference = b.tileW - a.tileW;
                if (difference !== 0) {
                    return difference;
                }
            }

            if ((randomize || attempt >= 5) &&
                    Math.abs(a._packRandom - b._packRandom) > EPSILON) {
                return a._packRandom - b._packRandom;
            }
            return a.order - b.order;
        });
        return ordered;
    }

    function calculateLayoutPatternMetrics(state) {
        var owners = [];
        var minimumColumn = state.columns;
        var maximumColumn = 0;
        var horizontalBand = 0;
        var verticalBand = 0;
        var longestHorizontal = 0;
        var longestVertical = 0;
        var sizeSum = 0;
        var verticalSum = 0;
        var sizeMean;
        var verticalMean;
        var sizeVariance = 0;
        var verticalVariance = 0;
        var sizeVerticalCovariance = 0;
        var sizeVerticalGradient = 0;
        var featureWeight = 0;
        var featureVerticalSum = 0;
        var featureCenterBias = 0;
        var sizeValue;
        var verticalValue;
        var weight;
        var usedWidth;
        var edgeCount;
        var longestRun;
        var run;
        var placement;
        var upper;
        var lower;
        var left;
        var right;
        var x;
        var y;
        var i;

        for (i = 0; i < state.placements.length; i++) {
            placement = state.placements[i];
            minimumColumn = Math.min(minimumColumn, placement.x);
            maximumColumn = Math.max(
                maximumColumn, placement.x + placement.item.tileW
            );
        }
        usedWidth = Math.max(1, maximumColumn - minimumColumn);
        for (y = 0; y < state.usedRows; y++) {
            owners[y] = [];
        }
        for (i = 0; i < state.placements.length; i++) {
            placement = state.placements[i];
            sizeValue = Math.log(Math.max(1, placement.item.tileArea));
            verticalValue = (placement.y + placement.item.tileH / 2) /
                Math.max(1, state.usedRows);
            sizeSum += sizeValue;
            verticalSum += verticalValue;
            weight = Math.max(0, placement.item.tileArea - 1);
            featureWeight += weight;
            featureVerticalSum += verticalValue * weight;
            for (y = placement.y; y < placement.y + placement.item.tileH; y++) {
                for (x = placement.x;
                        x < placement.x + placement.item.tileW; x++) {
                    owners[y][x - minimumColumn] = placement.item.id + 1;
                }
            }
        }

        if (state.placements.length > 1) {
            sizeMean = sizeSum / state.placements.length;
            verticalMean = verticalSum / state.placements.length;
            for (i = 0; i < state.placements.length; i++) {
                placement = state.placements[i];
                sizeValue = Math.log(Math.max(1, placement.item.tileArea));
                verticalValue = (placement.y + placement.item.tileH / 2) /
                    Math.max(1, state.usedRows);
                sizeVariance += (sizeValue - sizeMean) *
                    (sizeValue - sizeMean);
                verticalVariance += (verticalValue - verticalMean) *
                    (verticalValue - verticalMean);
                sizeVerticalCovariance += (sizeValue - sizeMean) *
                    (verticalValue - verticalMean);
            }
            if (sizeVariance > EPSILON && verticalVariance > EPSILON) {
                sizeVerticalGradient = Math.abs(sizeVerticalCovariance /
                    Math.sqrt(sizeVariance * verticalVariance));
            }
        }
        if (featureWeight > EPSILON) {
            featureCenterBias = Math.abs(featureVerticalSum / featureWeight - 0.5);
        }

        for (y = 1; y < state.usedRows; y++) {
            edgeCount = 0;
            longestRun = 0;
            run = 0;
            for (x = 0; x < usedWidth; x++) {
                upper = owners[y - 1][x];
                lower = owners[y][x];
                if (upper !== undefined && lower !== undefined && upper !== lower) {
                    edgeCount++;
                    run++;
                    longestRun = Math.max(longestRun, run);
                } else {
                    run = 0;
                }
            }
            horizontalBand += edgeCount * edgeCount +
                2 * longestRun * longestRun;
            longestHorizontal = Math.max(longestHorizontal, longestRun);
        }

        for (x = 1; x < usedWidth; x++) {
            edgeCount = 0;
            longestRun = 0;
            run = 0;
            for (y = 0; y < state.usedRows; y++) {
                left = owners[y][x - 1];
                right = owners[y][x];
                if (left !== undefined && right !== undefined && left !== right) {
                    edgeCount++;
                    run++;
                    longestRun = Math.max(longestRun, run);
                } else {
                    run = 0;
                }
            }
            verticalBand += edgeCount * edgeCount +
                2 * longestRun * longestRun;
            longestVertical = Math.max(longestVertical, longestRun);
        }

        return {
            usedWidth: usedWidth,
            fill: state.usedRows > 0 ?
                state.usedCells / (state.usedRows * usedWidth) : 1,
            horizontalBand: horizontalBand,
            verticalBand: verticalBand,
            longestHorizontal: longestHorizontal,
            longestVertical: longestVertical,
            sizeVerticalGradient: sizeVerticalGradient,
            featureCenterBias: featureCenterBias
        };
    }

    function summarizeState(state) {
        var pattern = calculateLayoutPatternMetrics(state);

        return {
            rows: state.usedRows,
            buried: state.buried,
            roughness: state.roughness,
            usedCells: state.usedCells,
            usedWidth: pattern.usedWidth,
            fill: pattern.fill,
            horizontalBand: pattern.horizontalBand,
            verticalBand: pattern.verticalBand,
            longestHorizontal: pattern.longestHorizontal,
            longestVertical: pattern.longestVertical,
            sizeVerticalGradient: pattern.sizeVerticalGradient,
            featureCenterBias: pattern.featureCenterBias
        };
    }

    function placementSignature(placements) {
        var sorted = placements.slice(0);
        var parts = [];
        var i;

        sorted.sort(function (a, b) {
            return a.item.id - b.item.id;
        });
        for (i = 0; i < sorted.length; i++) {
            parts.push(sorted[i].item.id + ":" + sorted[i].x + "," +
                sorted[i].y + "," + sorted[i].item.tileW + "x" + sorted[i].item.tileH);
        }
        return parts.join("|");
    }

    function layoutIsBetter(candidate, best, randomize) {
        if (!best) {
            return true;
        }
        if (candidate.metrics.rows !== best.metrics.rows) {
            return candidate.metrics.rows < best.metrics.rows;
        }
        if (candidate.metrics.buried !== best.metrics.buried) {
            return candidate.metrics.buried < best.metrics.buried;
        }
        if (candidate.metrics.roughness !== best.metrics.roughness) {
            return candidate.metrics.roughness < best.metrics.roughness;
        }
        if (randomize && Math.abs(candidate.randomTie - best.randomTie) > EPSILON) {
            return candidate.randomTie < best.randomTie;
        }
        return candidate.signature < best.signature;
    }

    function organicLayoutScore(candidate, baseline, packingStyle) {
        var metrics = candidate.metrics;
        var usedCells = Math.max(1, metrics.usedCells);
        var rowOver = Math.max(0, metrics.rows - baseline.metrics.rows);
        var buriedOver = Math.max(0, metrics.buried - baseline.metrics.buried);
        var horizontalWeight = packingStyle === "Loose Mosaic" ? 3.0 : 4.0;
        var verticalWeight = packingStyle === "Loose Mosaic" ? 0.5 : 0.8;
        var fillWeight = packingStyle === "Loose Mosaic" ? 30 : 60;
        var rowWeight = packingStyle === "Loose Mosaic" ? 1.5 : 3.0;
        var randomWeight = packingStyle === "Loose Mosaic" ? 1.0 : 0.35;
        var sizeGradientWeight = packingStyle === "Loose Mosaic" ? 42 : 20;
        var featureCenterWeight = packingStyle === "Loose Mosaic" ? 28 : 12;

        return horizontalWeight * metrics.horizontalBand / usedCells +
            verticalWeight * metrics.verticalBand / usedCells +
            (1 - metrics.fill) * fillWeight + rowOver * rowWeight +
            buriedOver * 0.5 +
            metrics.sizeVerticalGradient * sizeGradientWeight +
            metrics.featureCenterBias * featureCenterWeight +
            candidate.randomTie * randomWeight;
    }

    function selectPackedLayout(candidates, packingStyle, randomize,
            compactBaseline) {
        var baseline = compactBaseline || null;
        var best = null;
        var bestScore = Number.MAX_VALUE;
        var score;
        var rowSlack;
        var buriedSlack;
        var minimumFill;
        var candidate;
        var i;

        if (packingStyle === "Compact") {
            for (i = 0; i < candidates.length; i++) {
                if (layoutIsBetter(candidates[i], baseline, randomize)) {
                    baseline = candidates[i];
                }
            }
            return baseline;
        }

        if (!baseline) {
            for (i = 0; i < candidates.length; i++) {
                if (layoutIsBetter(candidates[i], baseline, false)) {
                    baseline = candidates[i];
                }
            }
        }
        if (!baseline) {
            return baseline;
        }

        rowSlack = packingStyle === "Loose Mosaic" ?
            Math.max(2, Math.ceil(baseline.metrics.rows * 0.08)) :
            Math.max(1, Math.ceil(baseline.metrics.rows * 0.04));
        buriedSlack = Math.ceil(baseline.metrics.usedCells *
            (packingStyle === "Loose Mosaic" ? 0.05 : 0.02));
        minimumFill = Math.min(
            packingStyle === "Loose Mosaic" ? 0.78 : 0.86,
            baseline.metrics.fill
        );

        for (i = 0; i < candidates.length; i++) {
            candidate = candidates[i];
            if (candidate.metrics.rows > baseline.metrics.rows + rowSlack ||
                    candidate.metrics.buried > baseline.metrics.buried + buriedSlack ||
                    candidate.metrics.fill + EPSILON < minimumFill) {
                continue;
            }
            score = organicLayoutScore(candidate, baseline, packingStyle);
            if (!best || score < bestScore - EPSILON ||
                    (Math.abs(score - bestScore) <= EPSILON &&
                    candidate.signature < best.signature)) {
                best = candidate;
                bestScore = score;
            }
        }
        return best || baseline;
    }

    function cloneGridState(state) {
        var grid = [];
        var i;

        for (i = 0; i < state.grid.length; i++) {
            grid[i] = state.grid[i] ? state.grid[i].slice(0) : [];
        }
        return {
            columns: state.columns,
            grid: grid,
            colHeights: state.colHeights.slice(0),
            colCounts: state.colCounts.slice(0),
            usedRows: state.usedRows,
            usedCells: state.usedCells,
            buried: state.buried,
            roughness: state.roughness,
            placements: state.placements.slice(0)
        };
    }

    function placementComparison(a, b, randomize, packingStyle) {
        if (candidateIsBetter(a, b, randomize, packingStyle)) {
            return -1;
        }
        if (candidateIsBetter(b, a, randomize, packingStyle)) {
            return 1;
        }
        return 0;
    }

    function findPlacementChoices(state, tile, limit, randomize, rng, packingStyle) {
        var choices = [];
        var candidate;
        var insertAt;
        var x;
        var y;

        for (y = 0; y <= state.usedRows; y++) {
            for (x = 0; x <= state.columns - tile.tileW; x++) {
                if (!canPlace(state, tile, x, y)) {
                    continue;
                }
                candidate = candidateMetrics(
                    state, tile, x, y, randomize, rng, packingStyle
                );
                insertAt = choices.length;
                while (insertAt > 0 &&
                        placementComparison(
                            candidate, choices[insertAt - 1], randomize,
                            packingStyle
                        ) < 0) {
                    insertAt--;
                }
                choices.splice(insertAt, 0, candidate);
                if (choices.length > limit) {
                    choices.pop();
                }
            }
        }
        return choices;
    }

    function removeArrayItem(values, index) {
        var result = [];
        var i;

        for (i = 0; i < values.length; i++) {
            if (i !== index) {
                result.push(values[i]);
            }
        }
        return result;
    }

    function remainingShapeSignature(remaining) {
        var counts = {};
        var keys = [];
        var parts = [];
        var key;
        var i;

        for (i = 0; i < remaining.length; i++) {
            key = remaining[i].tileW + "x" + remaining[i].tileH;
            if (counts[key] === undefined) {
                counts[key] = 1;
                keys.push(key);
            } else {
                counts[key]++;
            }
        }
        keys.sort();
        for (i = 0; i < keys.length; i++) {
            parts.push(keys[i] + ":" + counts[keys[i]]);
        }
        return parts.join("|");
    }

    function comparePartialNodes(a, b, totalTileArea, columns, randomize) {
        var lowerBound = Math.ceil(totalTileArea / columns);
        var projectedA = Math.max(lowerBound, a.state.usedRows);
        var projectedB = Math.max(lowerBound, b.state.usedRows);
        var emptyA;
        var emptyB;

        if (projectedA !== projectedB) {
            return projectedA - projectedB;
        }
        if (a.state.buried !== b.state.buried) {
            return a.state.buried - b.state.buried;
        }
        if (a.state.roughness !== b.state.roughness) {
            return a.state.roughness - b.state.roughness;
        }
        emptyA = a.state.usedRows * columns - a.state.usedCells;
        emptyB = b.state.usedRows * columns - b.state.usedCells;
        if (emptyA !== emptyB) {
            return emptyA - emptyB;
        }
        if (randomize && Math.abs(a.randomTie - b.randomTie) > EPSILON) {
            return a.randomTie < b.randomTie ? -1 : 1;
        }
        return a.sequence - b.sequence;
    }

    function selectBeamNodes(candidates, beamWidth, totalTileArea, columns, randomize) {
        var reservedByShape = {};
        var reserved = [];
        var selected = [];
        var signature;
        var node;
        var key;
        var i;

        candidates.sort(function (a, b) {
            return comparePartialNodes(a, b, totalTileArea, columns, randomize);
        });

        for (i = 0; i < candidates.length; i++) {
            node = candidates[i];
            signature = remainingShapeSignature(node.remaining);
            if (reservedByShape[signature] === undefined) {
                reservedByShape[signature] = node;
            }
        }
        for (key in reservedByShape) {
            if (reservedByShape.hasOwnProperty(key)) {
                reserved.push(reservedByShape[key]);
            }
        }
        reserved.sort(function (a, b) {
            return comparePartialNodes(a, b, totalTileArea, columns, randomize);
        });

        for (i = 0; i < reserved.length && selected.length < beamWidth; i++) {
            reserved[i]._beamSelected = true;
            selected.push(reserved[i]);
        }
        for (i = 0; i < candidates.length && selected.length < beamWidth; i++) {
            if (!candidates[i]._beamSelected) {
                candidates[i]._beamSelected = true;
                selected.push(candidates[i]);
            }
        }
        for (i = 0; i < selected.length; i++) {
            selected[i]._beamSelected = false;
        }
        return selected;
    }

    function beamPackTiles(items, columns, seed, randomize, packingStyle) {
        var beamWidth = items.length <= 10 ? 72 : (items.length <= 18 ? 40 : 24);
        var placementLimit = items.length <= 12 ? 5 : 3;
        var rng = new RNG((seed + 15485863) % 2147483647);
        var startingItems = randomize ? shuffledCopy(items, rng) : items.slice(0);
        var totalTileArea = 0;
        var beam = [];
        var expanded;
        var seenShapes;
        var shapeKey;
        var choices;
        var nextState;
        var nextRemaining;
        var sequence = 0;
        var node;
        var tile;
        var finalState;
        var best = null;
        var candidate;
        var depth;
        var n;
        var r;
        var p;
        var i;

        for (i = 0; i < items.length; i++) {
            totalTileArea += items[i].tileArea;
        }
        if (!randomize) {
            startingItems.sort(function (a, b) {
                return a.order - b.order;
            });
        }
        beam.push({
            state: makeGridState(columns),
            remaining: startingItems,
            randomTie: rng.next(),
            sequence: sequence++
        });

        for (depth = 0; depth < items.length; depth++) {
            expanded = [];
            for (n = 0; n < beam.length; n++) {
                node = beam[n];
                seenShapes = {};
                for (r = 0; r < node.remaining.length; r++) {
                    tile = node.remaining[r];
                    shapeKey = tile.tileW + "x" + tile.tileH;
                    if (seenShapes[shapeKey]) {
                        continue;
                    }
                    seenShapes[shapeKey] = true;
                    choices = findPlacementChoices(
                        node.state, tile, placementLimit, randomize, rng,
                        packingStyle
                    );
                    for (p = 0; p < choices.length; p++) {
                        nextState = cloneGridState(node.state);
                        occupy(nextState, tile, choices[p].x, choices[p].y, choices[p]);
                        nextRemaining = removeArrayItem(node.remaining, r);
                        expanded.push({
                            state: nextState,
                            remaining: nextRemaining,
                            randomTie: rng.next(),
                            sequence: sequence++
                        });
                    }
                }
            }
            if (expanded.length === 0) {
                return null;
            }
            beam = selectBeamNodes(
                expanded, beamWidth, totalTileArea, columns, randomize
            );
        }

        for (i = 0; i < beam.length; i++) {
            finalState = beam[i].state;
            candidate = {
                placements: finalState.placements,
                usedRows: finalState.usedRows,
                usedCells: finalState.usedCells,
                metrics: summarizeState(finalState),
                signature: placementSignature(finalState.placements),
                randomTie: beam[i].randomTie
            };
            if (layoutIsBetter(candidate, best, randomize)) {
                best = candidate;
            }
        }
        return best;
    }

    function makeGreedyPackCandidate(items, columns, seed, randomize, attempt,
            orderStyle, placementStyle) {
        var attemptSeed = (seed + (attempt + 1) * 104729) % 2147483647;
        var rng = new RNG(attemptSeed);
        var ordered = makeAttemptOrder(
            items, attempt, randomize, rng, orderStyle
        );
        var state = makeGridState(columns);
        var placement;
        var i;

        for (i = 0; i < ordered.length; i++) {
            placement = findBestPlacement(
                state, ordered[i], randomize, rng, placementStyle
            );
            if (!placement) {
                throw new Error("The grid packer could not place a tile.");
            }
            occupy(state, ordered[i], placement.x, placement.y, placement);
        }
        return {
            placements: state.placements,
            usedRows: state.usedRows,
            usedCells: state.usedCells,
            metrics: summarizeState(state),
            signature: placementSignature(state.placements),
            randomTie: rng.next()
        };
    }

    function packTiles(items, columns, seed, randomize, packingStyle) {
        var compactAttemptCount = items.length <= 80 ? (randomize ? 18 : 14) :
            (items.length <= 200 ? 8 : 5);
        var organicAttemptCount;
        var candidates = [];
        var compactCandidates = [];
        var compactBest;
        var compactBeam;
        var organicSeed;
        var dispersionSeed;
        var attempt;
        var beamCandidate;

        for (attempt = 0; attempt < compactAttemptCount; attempt++) {
            compactCandidates.push(makeGreedyPackCandidate(
                items, columns, seed, randomize, attempt, "Compact", "Compact"
            ));
        }
        if (items.length <= 28 && columns <= 60) {
            compactBeam = beamPackTiles(
                items, columns, seed, randomize, "Compact"
            );
            if (compactBeam) {
                compactCandidates.push(compactBeam);
            }
        }
        compactBest = selectPackedLayout(
            compactCandidates, "Compact", randomize, null
        );
        if (packingStyle === "Compact") {
            return compactBest;
        }

        if (items.length <= 80) {
            organicAttemptCount = randomize ? 18 : 14;
        } else if (items.length <= 200) {
            organicAttemptCount = 9;
        } else if (items.length <= 600) {
            organicAttemptCount = 7;
        } else {
            organicAttemptCount = 5;
        }
        organicSeed = (seed + 32452843) % 2147483647;
        candidates.push(compactBest);
        for (attempt = 0; attempt < organicAttemptCount; attempt++) {
            candidates.push(makeGreedyPackCandidate(
                items, columns, organicSeed, randomize, attempt,
                packingStyle, packingStyle
            ));
        }
        if (packingStyle === "Loose Mosaic") {
            dispersionSeed = (organicSeed + 49979687) % 2147483647;
            candidates.push(makeGreedyPackCandidate(
                items, columns, dispersionSeed, randomize, 3,
                "Loose Mosaic", "Compact"
            ));
            candidates.push(makeGreedyPackCandidate(
                items, columns, dispersionSeed, randomize, 6,
                "Loose Mosaic", "Compact"
            ));
        }

        if (items.length <= 28 && columns <= 60) {
            beamCandidate = beamPackTiles(
                items, columns, organicSeed, randomize, packingStyle
            );
            if (beamCandidate) {
                candidates.push(beamCandidate);
            }
        }
        return selectPackedLayout(
            candidates, packingStyle, randomize, compactBest
        );
    }

    function makeBaseSeed(items, settings, columns) {
        var seed = 13579;
        var varietySalt = settings.tileVariety === "Wild" ? 37 :
            (settings.tileVariety === "Bold" ? 23 : 11);
        var styleSalt = settings.packingStyle === "Loose Mosaic" ? 71 :
            (settings.packingStyle === "Interlocking" ? 53 : 0);
        var item;
        var i;

        seed = (seed * 48271 + Math.round(settings.unit * 10)) % 2147483647;
        seed = (seed * 48271 + Math.round(settings.gap * 10)) % 2147483647;
        seed = (seed * 48271 + columns * 97) % 2147483647;
        seed = (seed * 48271 + varietySalt) % 2147483647;
        if (styleSalt !== 0) {
            seed = (seed * 48271 + styleSalt +
                (settings.mixOrientations ? 101 : 0)) % 2147483647;
        }
        for (i = 0; i < items.length; i++) {
            item = items[i];
            seed = (seed * 48271 + item.layer.index * 7919 +
                Math.round(item.sourceWidth) * 31 + Math.round(item.sourceHeight) * 17) % 2147483647;
        }
        return Math.max(1, Math.floor(seed));
    }

    function findBentoMasks(maskParade) {
        var matches = [];
        var mask;
        var i;

        for (i = 1; i <= maskParade.numProperties; i++) {
            mask = maskParade.property(i);
            if (mask && mask.name === CROP_MASK_NAME) {
                matches.push(mask);
            }
        }
        return matches;
    }

    function copyValue(value) {
        if (value && value.constructor === Array) {
            return value.slice(0);
        }
        return value;
    }

    function requireStaticMaskProperty(mask, matchName, displayName) {
        var property = mask.property(matchName);

        if (!property || !propertyIsStatic(property)) {
            throw new Error("The Bento crop " + displayName +
                " is missing, animated, or expression-driven.");
        }
        return property;
    }

    function preflightBentoMask(layer, needsCrop) {
        var maskParade = layer.property("ADBE Mask Parade");
        var matches;
        var mask;
        var pathProperty;
        var featherProperty;
        var offsetProperty;
        var opacityProperty;
        var canAdd = false;
        var maskLocked = false;
        var info;

        if (!maskParade) {
            if (needsCrop) {
                throw new Error("Masks are unavailable on this layer.");
            }
            return null;
        }

        matches = findBentoMasks(maskParade);
        if (matches.length > 1) {
            throw new Error("Multiple masks use the reserved Bento crop name.");
        }
        if (matches.length === 0) {
            if (needsCrop) {
                try {
                    canAdd = maskParade.canAddProperty("ADBE Mask Atom");
                } catch (ignoreCanAdd) {
                    canAdd = false;
                }
                if (!canAdd) {
                    throw new Error("A crop mask cannot be added to this layer.");
                }
            }
            return {
                maskParade: maskParade,
                mask: null,
                existed: false,
                created: false,
                original: null
            };
        }

        mask = matches[0];
        try {
            maskLocked = mask.locked === true;
        } catch (ignoreMaskLocked) {
            maskLocked = false;
        }
        if (maskLocked) {
            throw new Error("The Bento crop mask is locked.");
        }

        info = {
            maskParade: maskParade,
            mask: mask,
            existed: true,
            created: false,
            original: {
                index: mask.propertyIndex,
                maskMode: mask.maskMode,
                inverted: mask.inverted,
                rotoBezier: mask.rotoBezier
            }
        };

        if (needsCrop) {
            pathProperty = requireStaticMaskProperty(mask, "ADBE Mask Shape", "path");
            featherProperty = requireStaticMaskProperty(mask, "ADBE Mask Feather", "feather");
            offsetProperty = requireStaticMaskProperty(mask, "ADBE Mask Offset", "expansion");
            opacityProperty = requireStaticMaskProperty(mask, "ADBE Mask Opacity", "opacity");
            info.original.pathValue = copyValue(pathProperty.value);
            info.original.featherValue = copyValue(featherProperty.value);
            info.original.offsetValue = copyValue(offsetProperty.value);
            info.original.opacityValue = copyValue(opacityProperty.value);
        }
        return info;
    }

    function restoreBentoMask(maskInfo) {
        var mask;

        if (!maskInfo) {
            return;
        }
        if (maskInfo.created) {
            mask = maskInfo.mask;
            if (mask) {
                mask.remove();
            }
            return;
        }
        if (!maskInfo.existed) {
            return;
        }

        mask = findBentoMasks(maskInfo.maskParade)[0];
        if (!mask) {
            throw new Error("The original Bento crop mask could not be restored.");
        }
        mask.maskMode = maskInfo.original.maskMode;
        mask.inverted = maskInfo.original.inverted;
        mask.rotoBezier = maskInfo.original.rotoBezier;
        if (maskInfo.original.pathValue !== undefined) {
            mask.property("ADBE Mask Shape").setValue(maskInfo.original.pathValue);
            mask.property("ADBE Mask Feather").setValue(maskInfo.original.featherValue);
            mask.property("ADBE Mask Offset").setValue(maskInfo.original.offsetValue);
            mask.property("ADBE Mask Opacity").setValue(maskInfo.original.opacityValue);
        }
        if (mask.propertyIndex !== maskInfo.original.index) {
            mask.moveTo(maskInfo.original.index);
        }
    }

    function fallbackCropVertices(item, tileRect, fitFactor) {
        var centerX = item.sourceWidth / 2;
        var centerY = item.sourceHeight / 2;
        var halfWidth = tileRect.width / (2 * fitFactor * item.parX);
        var halfHeight = tileRect.height / (2 * fitFactor);

        return [
            [centerX - halfWidth, centerY - halfHeight],
            [centerX + halfWidth, centerY - halfHeight],
            [centerX + halfWidth, centerY + halfHeight],
            [centerX - halfWidth, centerY + halfHeight]
        ];
    }

    function exactCropVertices(item, tileRect, fitFactor) {
        var layer = item.layer;
        var right = tileRect.left + tileRect.width;
        var bottom = tileRect.top + tileRect.height;

        try {
            if (typeof layer.compPointToSource === "function") {
                return [
                    layer.compPointToSource([tileRect.left, tileRect.top]),
                    layer.compPointToSource([right, tileRect.top]),
                    layer.compPointToSource([right, bottom]),
                    layer.compPointToSource([tileRect.left, bottom])
                ];
            }
        } catch (ignoreExactConversion) {
        }
        return fallbackCropVertices(item, tileRect, fitFactor);
    }

    function applyCropMask(item, tileRect, fitFactor, maskInfo) {
        var maskParade = maskInfo.maskParade;
        var cropMask = maskInfo.mask;
        var otherMaskCount;
        var shape;
        var vertices;
        var zeroTangents = [[0, 0], [0, 0], [0, 0], [0, 0]];
        var i;

        if (!cropMask) {
            cropMask = maskParade.addProperty("ADBE Mask Atom");
            maskInfo.mask = cropMask;
            maskInfo.created = true;
            cropMask.name = CROP_MASK_NAME;
        } else if (cropMask.propertyIndex !== maskParade.numProperties) {
            cropMask.moveTo(maskParade.numProperties);
            cropMask = findBentoMasks(maskParade)[0];
            maskInfo.mask = cropMask;
        }

        otherMaskCount = 0;
        for (i = 1; i <= maskParade.numProperties; i++) {
            if (i !== cropMask.propertyIndex &&
                    maskParade.property(i).maskMode !== MaskMode.NONE) {
                otherMaskCount++;
            }
        }

        vertices = exactCropVertices(item, tileRect, fitFactor);
        shape = new Shape();
        shape.vertices = vertices;
        shape.inTangents = zeroTangents;
        shape.outTangents = zeroTangents;
        shape.closed = true;

        cropMask.rotoBezier = false;
        cropMask.inverted = false;
        cropMask.maskMode = otherMaskCount > 0 ? MaskMode.INTERSECT : MaskMode.ADD;
        cropMask.property("ADBE Mask Shape").setValue(shape);
        cropMask.property("ADBE Mask Feather").setValue([0, 0]);
        cropMask.property("ADBE Mask Offset").setValue(0);
        cropMask.property("ADBE Mask Opacity").setValue(100);

        return otherMaskCount > 0;
    }

    function applyLayerToTile(item, tileRect, settings) {
        var fitFactor;
        var scalePercent;
        var anchor;
        var sourceCenterX;
        var sourceCenterY;
        var tileCenterX;
        var tileCenterY;
        var positionX;
        var positionY;
        var hadOtherMasks = false;
        var needsCrop = settings.fitMode === "Cover" && settings.cropCover;
        var maskInfo;
        var originalScale;
        var originalPosition;
        var rollbackProblems = [];

        if (settings.fitMode === "Cover") {
            fitFactor = Math.max(
                tileRect.width / item.displayWidth,
                tileRect.height / item.displayHeight
            );
        } else {
            fitFactor = Math.min(
                tileRect.width / item.displayWidth,
                tileRect.height / item.displayHeight
            );
        }
        scalePercent = fitFactor * 100;
        if (!isFiniteNumber(scalePercent) || scalePercent <= 0) {
            throw new Error("The calculated scale is invalid.");
        }

        anchor = item.anchorProperty.value;
        sourceCenterX = item.sourceWidth / 2;
        sourceCenterY = item.sourceHeight / 2;
        tileCenterX = tileRect.left + tileRect.width / 2;
        tileCenterY = tileRect.top + tileRect.height / 2;
        positionX = tileCenterX - (sourceCenterX - anchor[0]) * fitFactor * item.parX;
        positionY = tileCenterY - (sourceCenterY - anchor[1]) * fitFactor;

        maskInfo = preflightBentoMask(item.layer, needsCrop);
        originalScale = copyValue(item.scaleProperty.value);
        if (item.positionInfo.separated) {
            originalPosition = [
                item.positionInfo.xProperty.value,
                item.positionInfo.yProperty.value
            ];
        } else {
            originalPosition = copyValue(item.positionInfo.leader.value);
        }

        try {
            item.scaleProperty.setValue([scalePercent, scalePercent]);
            setStaticPosition(item.positionInfo, positionX, positionY);

            if (needsCrop) {
                hadOtherMasks = applyCropMask(item, tileRect, fitFactor, maskInfo);
            } else if (maskInfo && maskInfo.mask) {
                maskInfo.mask.maskMode = MaskMode.NONE;
            }
        } catch (applyError) {
            try {
                item.scaleProperty.setValue(originalScale);
                if (item.positionInfo.separated) {
                    item.positionInfo.xProperty.setValue(originalPosition[0]);
                    item.positionInfo.yProperty.setValue(originalPosition[1]);
                } else {
                    item.positionInfo.leader.setValue(originalPosition);
                }
            } catch (transformRestoreError) {
                rollbackProblems.push("Transform rollback failed");
            }
            try {
                restoreBentoMask(maskInfo);
            } catch (maskRestoreError) {
                rollbackProblems.push("mask rollback failed");
            }
            if (rollbackProblems.length > 0) {
                throw new Error(applyError.message + " (" + rollbackProblems.join(", ") +
                    "; use Undo to restore the complete run)");
            }
            throw applyError;
        }

        return hadOtherMasks;
    }

    function collectItems(comp, selectedLayers) {
        var items = [];
        var skipped = [];
        var inspection;
        var i;

        for (i = 0; i < selectedLayers.length; i++) {
            inspection = inspectLayer(selectedLayers[i], comp, i);
            if (inspection.item) {
                items.push(inspection.item);
            } else {
                skipped.push(layerLabel(selectedLayers[i]) + " — " + inspection.reason);
            }
        }
        return {items: items, skipped: skipped};
    }

    function makeTileRect(placement, unit, gap, originX, originY, minimumColumn) {
        return {
            left: originX + (placement.x - minimumColumn) * (unit + gap),
            top: originY + placement.y * (unit + gap),
            width: tilePixelWidth(placement.item.tileW, unit, gap),
            height: tilePixelWidth(placement.item.tileH, unit, gap)
        };
    }

    function makeDetailText(skipped, failed, warnings) {
        var lines = [];
        var i;

        if (warnings.length > 0) {
            lines.push("Warnings:");
            for (i = 0; i < warnings.length; i++) {
                lines.push("• " + warnings[i]);
            }
        }
        if (skipped.length > 0) {
            if (lines.length > 0) {
                lines.push("");
            }
            lines.push("Skipped:");
            for (i = 0; i < skipped.length && i < 20; i++) {
                lines.push("• " + skipped[i]);
            }
            if (skipped.length > 20) {
                lines.push("• …and " + (skipped.length - 20) + " more");
            }
        }
        if (failed.length > 0) {
            if (lines.length > 0) {
                lines.push("");
            }
            lines.push("Failed:");
            for (i = 0; i < failed.length && i < 20; i++) {
                lines.push("• " + failed[i]);
            }
            if (failed.length > 20) {
                lines.push("• …and " + (failed.length - 20) + " more");
            }
        }
        return lines.join("\n");
    }

    function readUISettings(ui) {
        var fitText = ui.fitMode.selection ? ui.fitMode.selection.text : "Cover";
        var varietyText = ui.tileVariety.selection ?
            ui.tileVariety.selection.text : "Balanced";
        var styleText = ui.packingStyle.selection ?
            ui.packingStyle.selection.text : "Interlocking";
        var unitExpression = trimText(ui.unit.text);
        var gapExpression = trimText(ui.gap.text);
        var widthExpression = trimText(ui.layoutWidth.text);
        var tileVariety = varietyText.indexOf("Wild") === 0 ? "Wild" :
            (varietyText.indexOf("Bold") === 0 ? "Bold" : "Balanced");
        var packingStyle = styleText.indexOf("Loose") === 0 ? "Loose Mosaic" :
            (styleText.indexOf("Compact") === 0 ? "Compact" : "Interlocking");

        return {
            unit: parsePositiveNumber(unitExpression, "Unit Size", false),
            gap: parsePositiveNumber(gapExpression, "Gap", true),
            layoutWidth: parsePositiveNumber(widthExpression, "Layout Width", false),
            unitExpression: unitExpression,
            gapExpression: gapExpression,
            layoutWidthExpression: widthExpression,
            fitMode: fitText === "Contain" ? "Contain" : "Cover",
            tileVariety: tileVariety,
            packingStyle: packingStyle,
            mixOrientations: ui.mixOrientations.value === true,
            cropCover: ui.cropCover.value === true,
            centerLayout: ui.centerLayout.value === true
        };
    }

    function runLayout(ui, randomize) {
        var comp = app.project ? app.project.activeItem : null;
        var settings;
        var selectedLayers;
        var collection;
        var items;
        var effectiveWidth;
        var columns;
        var actualGridWidth;
        var usedColumns;
        var minimumColumn;
        var maximumColumn;
        var baseSeed;
        var seed;
        var rng;
        var packed;
        var gridHeight;
        var originX;
        var originY;
        var successes = 0;
        var successfulCells = 0;
        var failed = [];
        var warnings = [];
        var existingMaskInteractions = 0;
        var orientationMixes = 0;
        var fillPercent;
        var placement;
        var tileRect;
        var detailText;
        var i;

        if (!(comp instanceof CompItem)) {
            alert("Open a composition, select bitmap/footage layers, and try again.", SCRIPT_NAME);
            return;
        }

        try {
            settings = readUISettings(ui);
        } catch (settingsError) {
            alert(settingsError.message, SCRIPT_NAME);
            return;
        }

        selectedLayers = comp.selectedLayers;
        if (!selectedLayers || selectedLayers.length === 0) {
            alert("Select at least one bitmap/footage layer in the active composition.", SCRIPT_NAME);
            return;
        }

        effectiveWidth = Math.min(settings.layoutWidth, Number(comp.width));
        if (settings.layoutWidth > comp.width + EPSILON) {
            warnings.push("Layout Width was capped to the composition width (" + comp.width + " px)." );
        }
        columns = Math.floor((effectiveWidth + settings.gap) / (settings.unit + settings.gap));
        if (columns < 1) {
            columns = 1;
            warnings.push("Unit Size is wider than the available layout width; a one-column grid was used.");
        }
        if (columns > MAX_COLUMNS) {
            columns = MAX_COLUMNS;
            warnings.push("The calculated column count was limited to " + MAX_COLUMNS +
                " for performance. Increase Unit Size or Gap to use the full width.");
        }

        collection = collectItems(comp, selectedLayers);
        items = collection.items;
        if (items.length === 0) {
            detailText = makeDetailText(collection.skipped, failed, warnings);
            alert("No supported layers can be arranged.\n\n" + detailText, SCRIPT_NAME);
            return;
        }

        baseSeed = makeBaseSeed(items, settings, columns);
        if (randomize) {
            randomCounter++;
            seed = (baseSeed + (new Date()).getTime() + randomCounter * 104729) % 2147483647;
        } else {
            seed = baseSeed;
        }
        rng = new RNG(seed);

        orientationMixes = assignTileSizes(
            items, columns, settings.unit, settings.gap, randomize, rng,
            settings.tileVariety, settings.packingStyle,
            settings.mixOrientations
        );
        if (orientationMixes > 0) {
            warnings.push(orientationMixes +
                " tile frame orientation(s) were mixed; layer pixels remain upright.");
        }
        try {
            packed = packTiles(
                items, columns, seed, randomize, settings.packingStyle
            );
        } catch (packingError) {
            alert("Packing failed: " + packingError.message, SCRIPT_NAME);
            return;
        }

        minimumColumn = columns;
        maximumColumn = 0;
        for (i = 0; i < packed.placements.length; i++) {
            minimumColumn = Math.min(minimumColumn, packed.placements[i].x);
            maximumColumn = Math.max(
                maximumColumn,
                packed.placements[i].x + packed.placements[i].item.tileW
            );
        }
        usedColumns = maximumColumn - minimumColumn;
        actualGridWidth = tilePixelWidth(usedColumns, settings.unit, settings.gap);
        gridHeight = tilePixelWidth(packed.usedRows, settings.unit, settings.gap);
        if (settings.centerLayout) {
            originX = actualGridWidth <= comp.width ? (comp.width - actualGridWidth) / 2 : 0;
            originY = gridHeight <= comp.height ? (comp.height - gridHeight) / 2 : 0;
        } else {
            originX = 0;
            originY = 0;
        }
        if (actualGridWidth > comp.width + EPSILON) {
            warnings.push("The grid is wider than the composition because Unit Size exceeds the available width.");
        }
        if (gridHeight > comp.height + EPSILON) {
            warnings.push("The grid is " + Math.round(gridHeight) +
                " px tall and extends below the composition. Reduce Unit Size/Gap to fit it.");
        }

        saveUISettings(settings);
        app.beginUndoGroup(SCRIPT_NAME + (randomize ? " — Randomize" : " — Repack"));
        try {
            for (i = 0; i < packed.placements.length; i++) {
                placement = packed.placements[i];
                tileRect = makeTileRect(
                    placement, settings.unit, settings.gap, originX, originY, minimumColumn
                );
                try {
                    if (applyLayerToTile(placement.item, tileRect, settings)) {
                        existingMaskInteractions++;
                    }
                    successes++;
                    successfulCells += placement.item.tileArea;
                } catch (layerError) {
                    failed.push(layerLabel(placement.item.layer) + " — " + layerError.message);
                }
            }
        } finally {
            app.endUndoGroup();
        }

        if (existingMaskInteractions > 0) {
            warnings.push(existingMaskInteractions +
                " layer(s) already had masks; the Bento crop uses Intersect mode on those layers.");
        }
        if (failed.length > 0) {
            warnings.push("Failed layer slots remain empty; use Undo to revert the complete run if needed.");
        }

        fillPercent = packed.usedRows > 0 && usedColumns > 0 ?
            Math.round((successfulCells / (packed.usedRows * usedColumns)) * 100) : 100;
        ui.status.text = successes + " arranged · " + usedColumns + " col × " +
            packed.usedRows + " row · " + fillPercent + "% grid fill" +
            (collection.skipped.length + failed.length > 0 ?
                " · " + (collection.skipped.length + failed.length) + " skipped/failed" : "");
        detailText = makeDetailText(collection.skipped, failed, warnings);
        ui.status.helpTip = detailText.length > 0 ? detailText :
            "All selected supported layers were arranged successfully.";
        if (failed.length > 0) {
            alert(failed.length + " layer(s) could not be completed.\n\n" +
                makeDetailText([], failed, warnings), SCRIPT_NAME);
        }
    }

    function deleteSelectedMasks(ui, deleteAll) {
        var comp = app.project ? app.project.activeItem : null;
        var selectedLayers;
        var targets = [];
        var skipped = [];
        var failed = [];
        var matchingCount = 0;
        var removableCount = 0;
        var lockedCount = 0;
        var removed = 0;
        var changedLayers = 0;
        var maskParade;
        var mask;
        var layer;
        var layerLocked;
        var maskLocked;
        var matchesScope;
        var layerMatchCount;
        var hasRemovable;
        var removedFromLayer;
        var scopeLabel = deleteAll ? "mask" : "Bento mask";
        var confirmationText;
        var detailText;
        var i;
        var j;

        if (!(comp instanceof CompItem)) {
            alert("Open a composition, select layers, and try again.", SCRIPT_NAME);
            return;
        }

        selectedLayers = comp.selectedLayers;
        if (!selectedLayers || selectedLayers.length === 0) {
            alert("Select at least one layer whose masks should be removed.",
                SCRIPT_NAME);
            return;
        }

        for (i = 0; i < selectedLayers.length; i++) {
            layer = selectedLayers[i];
            maskParade = layer.property("ADBE Mask Parade");
            if (!maskParade) {
                continue;
            }
            layerMatchCount = 0;
            hasRemovable = false;
            try {
                layerLocked = layer.locked === true;
            } catch (ignoreLayerLock) {
                layerLocked = true;
            }
            for (j = 1; j <= maskParade.numProperties; j++) {
                mask = maskParade.property(j);
                matchesScope = mask &&
                    (deleteAll || mask.name === CROP_MASK_NAME);
                if (!matchesScope) {
                    continue;
                }
                matchingCount++;
                layerMatchCount++;
                if (layerLocked) {
                    lockedCount++;
                    continue;
                }
                try {
                    maskLocked = mask.locked === true;
                } catch (ignoreMaskLock) {
                    maskLocked = true;
                }
                if (!maskLocked) {
                    hasRemovable = true;
                    removableCount++;
                } else {
                    lockedCount++;
                    skipped.push(layerLabel(layer) + " — " + mask.name +
                        " is locked");
                }
            }
            if (layerLocked && layerMatchCount > 0) {
                skipped.push(layerLabel(layer) + " — Layer is locked (" +
                    layerMatchCount + " " + scopeLabel +
                    (layerMatchCount === 1 ? "" : "s") + ")");
            }
            if (hasRemovable) {
                targets.push(layer);
            }
        }

        if (matchingCount === 0) {
            ui.status.text = deleteAll ?
                "No masks found on the selected layers." :
                "No Bento crop masks found on the selected layers.";
            ui.status.helpTip = deleteAll ?
                "No masks were changed." :
                "Only masks named " + CROP_MASK_NAME + " are removed.";
            return;
        }

        if (removableCount === 0) {
            ui.status.text = "No removable " + scopeLabel +
                (matchingCount === 1 ? "" : "s") + " · " +
                lockedCount + " locked";
            detailText = makeDetailText(skipped, failed, []);
            ui.status.helpTip = detailText.length > 0 ? detailText :
                "Locked masks and masks on locked layers were preserved.";
            alert("No selected " + scopeLabel +
                (matchingCount === 1 ? " could" : "s could") +
                " be removed.\n\n" + detailText, SCRIPT_NAME);
            return;
        }

        if (deleteAll) {
            confirmationText = "Delete ALL unlocked masks from the selected layers?\n\n" +
                removableCount + " mask" + (removableCount === 1 ? "" : "s") +
                " on " + targets.length + " layer" +
                (targets.length === 1 ? "" : "s") + " will be deleted.";
            if (lockedCount > 0) {
                confirmationText += "\n" + lockedCount +
                    " matching mask" + (lockedCount === 1 ? " is" : "s are") +
                    " locked or on locked layers and will be skipped.";
            }
            confirmationText += "\n\nThis includes user-created, animated, " +
                "expression-driven, disabled, and Bento masks. Mask indices and " +
                "expressions that reference them may change. Scale and Position " +
                "remain unchanged. Immediate Undo restores this operation.\n\nContinue?";
            if (!confirm(confirmationText, true, SCRIPT_NAME)) {
                ui.status.text = "All-mask deletion cancelled.";
                ui.status.helpTip = "No masks were changed.";
                return;
            }
        }

        app.beginUndoGroup(SCRIPT_NAME +
            (deleteAll ? " — Delete All Masks" : " — Clear Bento Masks"));
        try {
            for (i = 0; i < targets.length; i++) {
                layer = targets[i];
                removedFromLayer = 0;
                maskParade = layer.property("ADBE Mask Parade");
                for (j = maskParade.numProperties; j >= 1; j--) {
                    mask = maskParade.property(j);
                    matchesScope = mask &&
                        (deleteAll || mask.name === CROP_MASK_NAME);
                    if (!matchesScope) {
                        continue;
                    }
                    try {
                        maskLocked = mask.locked === true;
                    } catch (readLockError) {
                        maskLocked = true;
                    }
                    if (maskLocked) {
                        continue;
                    }
                    try {
                        mask.remove();
                        removed++;
                        removedFromLayer++;
                    } catch (removeError) {
                        failed.push(layerLabel(layer) + " — " + removeError.message);
                    }
                    maskParade = layer.property("ADBE Mask Parade");
                }
                if (removedFromLayer > 0) {
                    changedLayers++;
                }
            }
        } finally {
            app.endUndoGroup();
        }

        ui.status.text = removed + " " + scopeLabel +
            (removed === 1 ? "" : "s") +
            " removed from " + changedLayers + " layer" +
            (changedLayers === 1 ? "" : "s") +
            (lockedCount > 0 ? " · " + lockedCount + " locked" : "") +
            (failed.length > 0 ? " · " + failed.length + " failed" : "");
        detailText = makeDetailText(skipped, failed, []);
        ui.status.helpTip = detailText.length > 0 ? detailText :
            "Scale and Position were left unchanged. Use Undo to restore the masks.";
        if (failed.length > 0 || skipped.length > 0) {
            alert((deleteAll ? "All-mask deletion" : "Bento mask cleanup") +
                " completed with exceptions.\n\n" + detailText,
                SCRIPT_NAME);
        }
    }

    function clearBentoMasks(ui) {
        deleteSelectedMasks(ui, false);
    }

    function deleteAllSelectedMasks(ui) {
        deleteSelectedMasks(ui, true);
    }

    // ── 패널 연결부 ──────────────────────────────────────────
    function makeUiShim(settings) {
        return {
            unit:        { text: String(settings.unit) },
            gap:         { text: String(settings.gap) },
            layoutWidth: { text: String(settings.width) },
            fitMode:     { selection: { text: settings.fit } },
            tileVariety: { selection: { text: settings.variety } },
            packingStyle:{ selection: { text: settings.style } },
            mixOrientations: { value: settings.mix === true },
            cropCover:   { value: settings.crop === true },
            centerLayout:{ value: settings.center === true },
            status:      { text: "", helpTip: "" }
        };
    }
    return {
        run: function (settings, randomize) {
            __messages = [];
            var ui = makeUiShim(settings);
            runLayout(ui, randomize === true);
            return { status: ui.status.text, detail: ui.status.helpTip, messages: __messages };
        },
        clearMasks: function () {
            __messages = [];
            var ui = makeUiShim({ unit: 160, gap: 8, width: 1920, fit: "Cover", variety: "Balanced", style: "Interlocking" });
            clearBentoMasks(ui);
            return { status: ui.status.text, detail: ui.status.helpTip, messages: __messages };
        }
    };
})();

// 패널 진입점: settings JSON {unit,gap,width,fit,variety,style,mix,crop,center}
function bentoGrid(jsonStr, randomize) {
    var comp = app.project ? app.project.activeItem : null;
    if (!(comp && comp instanceof CompItem)) return err("활성 컴프가 없습니다.");
    var settings;
    try { settings = JSON.parse(jsonStr); } catch (e) { return err("설정 파싱 실패"); }
    var r = BANG_Bento.run(settings, randomize === true || randomize === "true");
    if (!r.status && r.messages.length) return err(r.messages.join(" / "));
    return ok({ status: r.status, detail: r.detail, messages: r.messages });
}
function bentoClearMasks() {
    var comp = app.project ? app.project.activeItem : null;
    if (!(comp && comp instanceof CompItem)) return err("활성 컴프가 없습니다.");
    var r = BANG_Bento.clearMasks();
    if (!r.status && r.messages.length) return err(r.messages.join(" / "));
    return ok({ status: r.status, detail: r.detail, messages: r.messages });
}
