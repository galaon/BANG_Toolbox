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
    var base  = "BANG_Null_";
    var count = 0;
    for (var i = 1; i <= comp.numLayers; i++) {
        var n = comp.layer(i).name;
        if (n.indexOf(base) === 0) {
            var num = parseInt(n.substr(base.length), 10);
            if (!isNaN(num) && num > count) count = num;
        }
    }
    var next = count + 1;
    if (next < 10)       return base + "00" + next;
    else if (next < 100) return base + "0"  + next;
    else                 return base + next;
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

// 선택한 기준 레이어의 마스크 영역 중심(컴프 공간)에 Null 을 배치한다.
//  · 마스크가 없으면 false 를 반환하고 위치를 건드리지 않는다(기존 동작 유지).
//  · 반드시 parent 연결 "이전"에 호출해야 한다 — 연결 후엔 toComp 가 순환된다.
//  레이어 공간 -> 컴프 공간 변환은 AE 표현식의 toComp() 를 잠시 빌려 정확히 수행
//  (부모/스케일/회전/3D 합성까지 AE 가 처리). 인덱스 참조라 동명 레이어도 안전.
function _positionNullAtMaskCenter(nullLayer, refLayer, time) {
    var maskRect = _maskBoundsAtTime(refLayer, time);
    if (maskRect === null) return false;

    var cx = maskRect.left + maskRect.width  / 2;
    var cy = maskRect.top  + maskRect.height / 2;

    var posProp = nullLayer.property("ADBE Transform Group").property("ADBE Position");
    var expr = 'thisComp.layer(' + refLayer.index + ').toComp([' + cx + ',' + cy + ']);';

    try {
        posProp.expression = expr;
        var world = posProp.value;     // 표현식 평가 결과(컴프 공간)
        posProp.expression = "";       // 표현식 제거 -> 정적 값으로 고정
        if (!nullLayer.threeDLayer && world.length > 2) {
            world = [world[0], world[1]];
        }
        posProp.setValue(world);
        return true;
    } catch (e) {
        try { posProp.expression = ""; } catch (e2) {}
        return false;
    }
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

        nullLayer.name            = buildNullName(comp);
        nullLayer.label           = 9;    // Green
        nullLayer.adjustmentLayer = true; // Adjustment Layer 활성화

        nullLayer
            .property("ADBE Transform Group")
            .property("ADBE Anchor Point")
            .setValue([50, 50, 0]);

        // 마스크가 있는 첫 선택 레이어를 기준으로 Null 을 마스크 영역 중심에 배치.
        // (부모 연결 전에 수행 — toComp 순환 방지. 마스크 없으면 기존 위치 유지)
        var maskCentered = false;
        for (var r = 0; r < selected.length; r++) {
            if (_positionNullAtMaskCenter(nullLayer, selected[r], comp.time)) {
                maskCentered = true;
                break;
            }
        }

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
