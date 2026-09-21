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
    app.beginUndoGroup("BANG Precomp Fit");
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
//  Cloner — Cinema 4D MoGraph Cloner 를 참고한 라이브 복제
//  · 선택한 소스 레이어 1개에 "Cloner …" 컨트롤 이펙트를 삽입한다 (사용자 결정 사항).
//  · 클론 = 소스의 복제본. 각 클론은 "Cloner Source"(Layer Control) 와
//    "Clone Index"(Slider) 를 갖고, Position/Rotation/Scale/Opacity 표현식이
//    소스의 컨트롤 값 + 자기 인덱스로 배치를 계산한다 → 생성 후에도 실시간 조정.
//  · 소스 자신 = 클론 #0 (표현식 없음). 소스를 옮기면 전체가 따라온다.
//    Radial 모드는 소스가 원 위(시작 각)에 놓이도록 원 중심을 역산한다.
//  · Count 변경은 레이어 수를 바꿔야 하므로 버튼 재실행(Re-clone):
//    소스에 이미 컨트롤이 있으면 기존 클론을 지우고 "Cloner Count" 값으로 다시 만든다.
// ============================================================

var CL_PREFIX = "Cloner ";

function _clFindEffect(layer, name) {
    var fx = layer.property("ADBE Effect Parade");
    for (var i = 1; i <= fx.numProperties; i++) if (fx.property(i).name === name) return fx.property(i);
    return null;
}

function _clAddControl(layer, matchName, name, value) {
    var fx = layer.property("ADBE Effect Parade");
    var ctl = fx.addProperty(matchName);
    ctl.name = name;
    if (value !== undefined && value !== null) {
        try { ctl.property(1).setValue(value); } catch (e) {}
    }
    return ctl;
}

// 소스 레이어에 컨트롤 세트 삽입 (이미 있으면 건너뜀). 반환: 새로 만들었는지
function _clEnsureControls(src, count, modeIndex) {
    if (_clFindEffect(src, CL_PREFIX + "Count") !== null) return false;
    var is3D = false;
    try { is3D = src.threeDLayer === true; } catch (e) {}

    // Mode: Dropdown(AE 17+) — 실패 시 Slider(1=Linear 2=Grid 3=Radial)
    var modeCtl = null;
    try {
        modeCtl = _clAddControl(src, "ADBE Dropdown Control", CL_PREFIX + "Mode");
        var menu = modeCtl.property(1).setPropertyParameters(["Linear", "Grid", "Radial"]);
        // setPropertyParameters 는 이펙트를 재생성하며 이름을 초기화한다 → 다시 지정
        menu.parentProperty.name = CL_PREFIX + "Mode";
        menu.setValue(modeIndex);
    } catch (eDD) {
        try { if (modeCtl) modeCtl.remove(); } catch (e2) {}
        _clAddControl(src, "ADBE Slider Control", CL_PREFIX + "Mode", modeIndex);
    }
    _clAddControl(src, "ADBE Slider Control",  CL_PREFIX + "Count", count);
    _clAddControl(src, "ADBE Point3D Control", CL_PREFIX + "Offset", [120, 0, 0]);             // Linear: 클론당 오프셋
    _clAddControl(src, "ADBE Slider Control",  CL_PREFIX + "Grid Columns", 3);
    _clAddControl(src, "ADBE Point3D Control", CL_PREFIX + "Grid Spacing", [150, 150, 0]);
    _clAddControl(src, "ADBE Slider Control",  CL_PREFIX + "Radius", 300);
    _clAddControl(src, "ADBE Angle Control",   CL_PREFIX + "Start Angle", 0);
    _clAddControl(src, "ADBE Angle Control",   CL_PREFIX + "End Angle", 360);
    _clAddControl(src, "ADBE Checkbox Control",CL_PREFIX + "Align to Radius", 0);
    _clAddControl(src, "ADBE Angle Control",   CL_PREFIX + "Step Rotation", 0);
    _clAddControl(src, "ADBE Slider Control",  CL_PREFIX + "Step Scale %", 0);
    _clAddControl(src, "ADBE Slider Control",  CL_PREFIX + "Step Opacity", 0);
    _clAddControl(src, "ADBE Slider Control",  CL_PREFIX + "Random Seed", 1);
    _clAddControl(src, "ADBE Point3D Control", CL_PREFIX + "Random Position", [0, 0, 0]);
    _clAddControl(src, "ADBE Angle Control",   CL_PREFIX + "Random Rotation", 0);
    _clAddControl(src, "ADBE Slider Control",  CL_PREFIX + "Random Scale %", 0);
    return true;
}

// 클론 판별: Clone Index + Cloner Source 이펙트를 갖고, Source 가 src 를 가리키는 레이어
function _clFindClones(comp, src) {
    var out = [];
    for (var i = 1; i <= comp.numLayers; i++) {
        var ly = comp.layer(i);
        if (ly === src) continue;
        var idx = _clFindEffect(ly, "Clone Index");
        var ref = _clFindEffect(ly, "Cloner Source");
        if (idx === null || ref === null) continue;
        var target = null;
        try { target = ref.property(1).value; } catch (e) {}   // Layer Control → 레이어 index
        if (target === src.index) out.push(ly);
    }
    return out;
}

// ── 표현식 텍스트 ────────────────────────────────────────────
// 공통 프리앰블: 소스·인덱스·모드·카운트
var CL_EXPR_HEAD =
    'var src = effect("Cloner Source")(1);\n' +
    'var i = effect("Clone Index")(1).value;\n' +
    'function C(n){ return src.effect("Cloner " + n)(1); }\n' +
    'var mode = Math.round(C("Mode").value);\n' +
    'var n = Math.max(1, Math.round(C("Count").value));\n' +
    'seedRandom(C("Random Seed").value + i, true);\n';

var CL_EXPR_ANGLE =
    'var a0 = degreesToRadians(C("Start Angle").value), a1 = degreesToRadians(C("End Angle").value);\n' +
    'var span = a1 - a0;\n' +
    'var full = Math.abs(Math.abs(span) - 2*Math.PI) < 1e-6;\n' +
    'var t = 0; if (full) { t = i / n; } else if (n > 1) { t = i / (n - 1); }\n' +   // 레거시 표현식 엔진의 중첩 삼항 버그 회피
    'var ang = a0 + span * t;\n';

function _clExprPosition(is3D) {
    return CL_EXPR_HEAD +
    'var base = src.transform.position.value;\n' +
    'if (base.length < 3) base = [base[0], base[1], 0];\n' +
    'var p = base;\n' +
    'if (mode == 1) {\n' +
    '  var off = C("Offset").value; p = base + off * i;\n' +
    '} else if (mode == 2) {\n' +
    '  var cols = Math.max(1, Math.round(C("Grid Columns").value));\n' +
    '  var sp = C("Grid Spacing").value;\n' +
    '  p = base + [sp[0] * (i % cols), sp[1] * Math.floor(i / cols), 0];\n' +
    '} else {\n' +
    CL_EXPR_ANGLE +
    '  var r = C("Radius").value;\n' +
    '  var center = base - [Math.cos(a0) * r, Math.sin(a0) * r, 0];\n' +   // 소스(i=0)가 시작각에 오도록
    '  p = center + [Math.cos(ang) * r, Math.sin(ang) * r, 0];\n' +
    '}\n' +
    'var rp = C("Random Position").value;\n' +
    'p = p + [random(-rp[0], rp[0]), random(-rp[1], rp[1]), random(-rp[2], rp[2])];\n' +
    (is3D ? 'p' : '[p[0], p[1]]');
}

function _clExprRotation() {
    return CL_EXPR_HEAD +
    'var rot = src.transform.rotation.value + C("Step Rotation").value * i;\n' +
    'if (mode == 3 && C("Align to Radius").value == 1) {\n' +
    CL_EXPR_ANGLE +
    '  rot += radiansToDegrees(ang - a0);\n' +
    '}\n' +
    'var rr = C("Random Rotation").value;\n' +
    'rot + random(-rr, rr)';
}

function _clExprScale(is3D) {
    return CL_EXPR_HEAD +
    'var s = src.transform.scale.value;\n' +
    'var f = 1 + (C("Step Scale %").value / 100) * i;\n' +
    'var rs = C("Random Scale %").value / 100;\n' +
    'f = f * (1 + random(-rs, rs));\n' +
    'if (f < 0) f = 0;\n' +
    (is3D ? '[s[0] * f, s[1] * f, (s.length > 2 ? s[2] : 100) * f]' : '[s[0] * f, s[1] * f]');
}

function _clExprOpacity() {
    return CL_EXPR_HEAD +
    'clamp(src.transform.opacity.value + C("Step Opacity").value * i, 0, 100)';
}

function _clPad3(n) { return (n < 10 ? "00" : (n < 100 ? "0" : "")) + n; }

// 패널 진입점
//   count: 총 개수(소스 포함). modeIndex: 1 Linear · 2 Grid · 3 Radial (최초 생성 시 초기값)
function createCloner(count, modeIndex) {
    var comp = app.project ? app.project.activeItem : null;
    if (!(comp && comp instanceof CompItem)) return err("활성 컴프가 없습니다.");
    var sel = comp.selectedLayers;
    if (sel.length !== 1) return err("소스 레이어를 하나만 선택해 주세요.");
    var src = sel[0];
    if (src instanceof CameraLayer || src instanceof LightLayer) return err("카메라·라이트는 복제할 수 없습니다.");
    if (_clFindEffect(src, "Clone Index") !== null) return err("클론이 아닌 소스 레이어를 선택해 주세요.");

    count = Math.round(parseFloat(count));
    if (isNaN(count) || count < 2) count = 2;
    if (count > 500) count = 500;
    modeIndex = Math.round(parseFloat(modeIndex)); if (!(modeIndex >= 1 && modeIndex <= 3)) modeIndex = 1;

    var is3D = false;
    try { is3D = src.threeDLayer === true; } catch (e) {}

    app.beginUndoGroup("BANG Cloner");
    try {
        var created = _clEnsureControls(src, count, modeIndex);
        if (!created) {
            // Re-clone: 소스의 Count 슬라이더가 정본
            var cnt = _clFindEffect(src, CL_PREFIX + "Count");
            var v = Math.round(cnt.property(1).value);
            if (v >= 2 && v <= 500) count = v; else cnt.property(1).setValue(count);
        }
        // 기존 클론 제거
        var old = _clFindClones(comp, src);
        for (var o = old.length - 1; o >= 0; o--) old[o].remove();

        // 소스 이름에서 기존 번호 접미 제거 없이 그대로 사용
        var baseName = src.name;
        var made = [];
        var prev = src;
        for (var k = 1; k < count; k++) {
            var cl = src.duplicate();
            cl.moveAfter(prev);
            prev = cl;
            cl.name = baseName + " • " + _clPad3(k);
            cl.selected = false;

            // 복제된 Cloner 컨트롤 제거 (컨트롤은 소스에만)
            var fx = cl.property("ADBE Effect Parade");
            for (var f = fx.numProperties; f >= 1; f--) {
                if (fx.property(f).name.indexOf(CL_PREFIX) === 0) fx.property(f).remove();
            }
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
            made.push(cl);
        }
        src.selected = true;
        app.endUndoGroup();
        return ok({ source: src.name, count: count, clones: made.length, recloned: !created, is3D: is3D });
    } catch (eMain) {
        app.endUndoGroup();
        return err(eMain.toString());
    }
}

// 클론 제거(소스 선택 상태에서) — 컨트롤은 남김
function removeClones() {
    var comp = app.project ? app.project.activeItem : null;
    if (!(comp && comp instanceof CompItem)) return err("활성 컴프가 없습니다.");
    var sel = comp.selectedLayers;
    if (sel.length !== 1) return err("소스 레이어를 하나만 선택해 주세요.");
    var src = sel[0];
    var old = _clFindClones(comp, src);
    if (old.length === 0) return err("이 레이어의 클론이 없습니다.");
    app.beginUndoGroup("BANG Cloner Remove");
    for (var o = old.length - 1; o >= 0; o--) old[o].remove();
    app.endUndoGroup();
    return ok({ removed: old.length });
}
