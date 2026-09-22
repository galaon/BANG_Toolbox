#target aftereffects

/*
    Bento Grid for Adobe After Effects
    Version 1.3.0

    Arranges selected 2D footage/precomp layers on a modular occupancy grid.
    The script is written for the legacy ExtendScript (ECMAScript 3) runtime.
*/

(function BentoGridPanel(thisObj) {
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
            throw new Error(label + (allowZero ? " 값은 0 이상이어야 합니다." : " 값은 0보다 커야 합니다."));
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
        var isVector = false;        // BANG_Toolbox: 셰이프/텍스트 레이어 — 내용 경계(sourceRectAtTime)로 측정
        var contentRect = null;
        var centerX = 0, centerY = 0;

        try {
            isVector = (layer instanceof ShapeLayer) || (layer instanceof TextLayer);
            // (ExtendScript 의 ShapeLayer/TextLayer 는 instanceof AVLayer 가 false 로 나올 수 있어 따로 허용)
            if ((!isVector && !(layer instanceof AVLayer)) || !layer.hasVideo || layer.nullLayer || layer.adjustmentLayer) {
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
        if (!source && !isVector) {
            return {reason: "no measurable source"};
        }

        if (!isVector) {
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
            if (!isVector && layer.collapseTransformation) {
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
            if (isVector) {
                contentRect = layer.sourceRectAtTime(comp.time, false);
                sourceWidth = Number(contentRect.width);
                sourceHeight = Number(contentRect.height);
                centerX = Number(contentRect.left) + sourceWidth / 2;    // 레이어 공간(중심 원점) — 앵커와 같은 공간
                centerY = Number(contentRect.top) + sourceHeight / 2;
            } else {
                sourceWidth = Number(layer.width);
                sourceHeight = Number(layer.height);
                centerX = sourceWidth / 2;
                centerY = sourceHeight / 2;
            }
        } catch (dimensionError) {
            return {reason: "unreadable source dimensions"};
        }
        if (!isFiniteNumber(sourceWidth) || !isFiniteNumber(sourceHeight) ||
                sourceWidth <= 0 || sourceHeight <= 0) {
            return {reason: "zero or invalid source dimensions"};
        }

        try {
            sourcePAR = isVector ? 1 : Number(source.pixelAspect);
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
                sourceCenterX: centerX,
                sourceCenterY: centerY,
                isVector: isVector,
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
        var centerX = item.sourceCenterX + (item.isVector ? item.layer.width / 2 : 0);
        var centerY = item.sourceCenterY + (item.isVector ? item.layer.height / 2 : 0);
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
        sourceCenterX = item.sourceCenterX;
        sourceCenterY = item.sourceCenterY;
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
        // BANG_Toolbox 한글 UI: 드롭다운은 인덱스로 판별 (표시 텍스트는 한글)
        var fitIdx = ui.fitMode.selection ? ui.fitMode.selection.index : 0;
        var varietyIdx = ui.tileVariety.selection ? ui.tileVariety.selection.index : 0;
        var styleIdx = ui.packingStyle.selection ? ui.packingStyle.selection.index : 1;
        var fitText = fitIdx === 1 ? "Contain" : "Cover";
        var varietyText = varietyIdx === 2 ? "Wild" : (varietyIdx === 1 ? "Bold" : "Balanced");
        var styleText = styleIdx === 2 ? "Loose Mosaic" : (styleIdx === 0 ? "Compact" : "Interlocking");
        var unitExpression = trimText(ui.unit.text);
        var gapExpression = trimText(ui.gap.text);
        var widthExpression = trimText(ui.layoutWidth.text);
        var tileVariety = varietyText.indexOf("Wild") === 0 ? "Wild" :
            (varietyText.indexOf("Bold") === 0 ? "Bold" : "Balanced");
        var packingStyle = styleText.indexOf("Loose") === 0 ? "Loose Mosaic" :
            (styleText.indexOf("Compact") === 0 ? "Compact" : "Interlocking");

        return {
            unit: parsePositiveNumber(unitExpression, "셀 크기", false),
            gap: parsePositiveNumber(gapExpression, "간격", true),
            layoutWidth: parsePositiveNumber(widthExpression, "최대 너비", false),
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
            alert("컴프를 열고 이미지·푸티지·프리컴프 레이어를 선택한 뒤 다시 실행하세요.", SCRIPT_NAME);
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
            alert("활성 컴프에서 이미지·푸티지·프리컴프 레이어를 하나 이상 선택하세요.", SCRIPT_NAME);
            return;
        }

        effectiveWidth = Math.min(settings.layoutWidth, Number(comp.width));
        if (settings.layoutWidth > comp.width + EPSILON) {
            warnings.push("최대 너비가 컴프 너비(" + comp.width + " px)로 제한되었습니다.");
        }
        columns = Math.floor((effectiveWidth + settings.gap) / (settings.unit + settings.gap));
        if (columns < 1) {
            columns = 1;
            warnings.push("셀 크기가 최대 너비보다 커서 1열 그리드로 배치했습니다.");
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
            alert("배치할 수 있는 레이어가 없습니다.\n\n" + detailText, SCRIPT_NAME);
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
            alert("배치 실패: " + packingError.message, SCRIPT_NAME);
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
                "개 레이어에 이미 마스크가 있어 Bento 크롭 마스크를 Intersect 모드로 추가했습니다.");
        }
        if (failed.length > 0) {
            warnings.push("실패한 레이어의 자리는 비어 있습니다. 전체를 되돌리려면 실행 취소(Ctrl+Z)하세요.");
        }

        fillPercent = packed.usedRows > 0 && usedColumns > 0 ?
            Math.round((successfulCells / (packed.usedRows * usedColumns)) * 100) : 100;
        ui.status.text = successes + "개 배치 · " + usedColumns + "열 × " +
            packed.usedRows + "행 · 채움 " + fillPercent + "%" +
            (collection.skipped.length + failed.length > 0 ?
                " · 건너뜀/실패 " + (collection.skipped.length + failed.length) + "개" : "");
        detailText = makeDetailText(collection.skipped, failed, warnings);
        ui.status.helpTip = detailText.length > 0 ? detailText :
            "선택한 레이어를 모두 배치했습니다.";
        if (failed.length > 0) {
            alert(failed.length + "개 레이어를 완료하지 못했습니다.\n\n" +
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
        var scopeLabel = deleteAll ? "마스크" : "Bento 마스크";
        var confirmationText;
        var detailText;
        var i;
        var j;

        if (!(comp instanceof CompItem)) {
            alert("컴프를 열고 레이어를 선택한 뒤 다시 실행하세요.", SCRIPT_NAME);
            return;
        }

        selectedLayers = comp.selectedLayers;
        if (!selectedLayers || selectedLayers.length === 0) {
            alert("마스크를 제거할 레이어를 하나 이상 선택하세요.",
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
                "선택한 레이어에 마스크가 없습니다." :
                "선택한 레이어에 Bento 크롭 마스크가 없습니다.";
            ui.status.helpTip = deleteAll ?
                "변경된 마스크가 없습니다." :
                "이름이 " + CROP_MASK_NAME + " 인 마스크만 제거합니다.";
            return;
        }

        if (removableCount === 0) {
            ui.status.text = "제거할 수 있는 " + scopeLabel + "가 없습니다 · 잠김 " + lockedCount + "개";
            detailText = makeDetailText(skipped, failed, []);
            ui.status.helpTip = detailText.length > 0 ? detailText :
                "잠긴 마스크와 잠긴 레이어의 마스크는 그대로 두었습니다.";
            alert("선택한 " + scopeLabel + "를 제거할 수 없습니다.\n\n" + detailText, SCRIPT_NAME);
            return;
        }

        if (deleteAll) {
            confirmationText = "선택한 레이어의 잠기지 않은 마스크를 전부 삭제할까요?\n\n" +
                targets.length + "개 레이어의 마스크 " + removableCount + "개가 삭제됩니다.";
            if (lockedCount > 0) {
                confirmationText += "\n잠겨 있거나 잠긴 레이어에 있는 마스크 " + lockedCount + "개는 건너뜁니다.";
            }
            confirmationText += "\n\n직접 만든 마스크, 애니메이션·표현식이 있는 마스크, 꺼진 마스크, Bento 마스크가 모두 포함됩니다. " +
                "마스크 번호와 그것을 참조하는 표현식이 달라질 수 있습니다. 크기·위치는 바뀌지 않으며 바로 실행 취소(Ctrl+Z)로 되돌릴 수 있습니다.\n\n계속할까요?";
            if (!confirm(confirmationText, true, SCRIPT_NAME)) {
                ui.status.text = "마스크 전체 삭제를 취소했습니다.";
                ui.status.helpTip = "변경된 마스크가 없습니다.";
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

        ui.status.text = changedLayers + "개 레이어에서 " + scopeLabel + " " + removed + "개 제거" +
            (lockedCount > 0 ? " · 잠김 " + lockedCount + "개" : "") +
            (failed.length > 0 ? " · 실패 " + failed.length + "개" : "");
        detailText = makeDetailText(skipped, failed, []);
        ui.status.helpTip = detailText.length > 0 ? detailText :
            "크기·위치는 바뀌지 않았습니다. 실행 취소(Ctrl+Z)로 마스크를 되돌릴 수 있습니다.";
        if (failed.length > 0 || skipped.length > 0) {
            alert((deleteAll ? "마스크 전체 삭제" : "Bento 마스크 제거") +
                "를 예외와 함께 마쳤습니다.\n\n" + detailText,
                SCRIPT_NAME);
        }
    }

    function clearBentoMasks(ui) {
        deleteSelectedMasks(ui, false);
    }

    function deleteAllSelectedMasks(ui) {
        deleteSelectedMasks(ui, true);
    }

    function addLabeledField(parent, labelText, initialValue, helpTip) {
        var group = parent.add("group");
        var label = group.add("statictext", undefined, labelText);
        var field = group.add("edittext", undefined, initialValue);

        group.orientation = "row";
        group.alignChildren = ["left", "center"];
        label.preferredSize.width = 105;
        field.characters = 9;
        field.helpTip = helpTip;
        return field;
    }

    // ── 강조 버튼 (ScriptUI 버튼은 색을 못 바꾸므로 iconbutton 에 직접 그림) ──
    function addAccentButton(parent, text, helpTip) {
        var btn = parent.add("iconbutton", undefined, undefined, {style: "toolbutton"});
        btn.preferredSize = [-1, 34];
        btn.helpTip = helpTip;
        btn.text = text;
        btn.onDraw = function () {
            var g = this.graphics;
            var w = this.size.width, h = this.size.height;
            var fill = g.newBrush(g.BrushType.SOLID_COLOR, this.__hover ? [0.22, 0.66, 1, 1] : [0.0, 0.6, 1, 1]);
            g.newPath();
            g.rectPath(0, 0, w, h);
            g.fillPath(fill);
            var font = ScriptUI.newFont(g.font.name, "BOLD", 13);
            var tw = g.measureString(this.text, font)[0];
            var th = g.measureString(this.text, font)[1];
            g.drawString(this.text, g.newPen(g.PenType.SOLID_COLOR, [1, 1, 1, 1], 1), (w - tw) / 2, (h - th) / 2, font);
        };
        btn.addEventListener("mouseover", function () { this.__hover = true; this.notify("onDraw"); });
        btn.addEventListener("mouseout", function () { this.__hover = false; this.notify("onDraw"); });
        return btn;
    }

    function buildUI(owner) {
        var palette = owner instanceof Panel ? owner :
            new Window("palette", SCRIPT_NAME, undefined, {resizeable: true});
        var header, guide, sizePanel, layoutPanel, optionPanel, buttons, maskRow;
        var g, label;
        var ui = {};
        var savedFit, savedVariety, savedStyle;

        if (!palette) {
            return null;
        }

        palette.orientation = "column";
        palette.alignChildren = ["fill", "top"];
        palette.spacing = 6;
        palette.margins = 10;
        try { palette.graphics.backgroundColor = palette.graphics.newBrush(palette.graphics.BrushType.SOLID_COLOR, [0.16, 0.16, 0.17, 1]); } catch (eBg) {}

        // 제목
        header = palette.add("group");
        header.orientation = "row";
        header.alignChildren = ["fill", "center"];
        ui.title = header.add("statictext", undefined, "Bento Grid — 선택한 이미지 레이어를 벤토 그리드로 배치");
        ui.title.alignment = ["fill", "center"];
        ui.version = header.add("statictext", undefined, "v" + VERSION);
        ui.version.alignment = ["right", "center"];

        // 사용 설명
        guide = palette.add("panel", undefined, "사용법");
        guide.orientation = "column";
        guide.alignChildren = ["fill", "top"];
        guide.margins = [10, 12, 10, 8];
        ui.guide = guide.add("statictext", undefined,
            "1. 컴프에서 배치할 이미지·푸티지·프리컴프 레이어를 선택합니다.\n" +
            "2. 셀 크기·간격·최대 너비를 정하고 [Bento Grid 적용]을 누릅니다.\n" +
            "3. 결과가 마음에 들지 않으면 [무작위 배치]로 큰 타일의 크기와 순서를 바꿔 다시 배치합니다.\n" +
            "· 크기·위치는 레이어의 Scale/Position 으로, 채우기 크롭은 " + CROP_MASK_NAME + " 마스크로 적용됩니다.\n" +
            "· 되돌리려면 실행 취소(Ctrl+Z), 마스크만 지우려면 [Bento 마스크 제거].",
            {multiline: true});
        ui.guide.preferredSize.height = 96;

        // 크기
        sizePanel = palette.add("panel", undefined, "크기");
        sizePanel.orientation = "column";
        sizePanel.alignChildren = ["fill", "top"];
        sizePanel.spacing = 4;
        sizePanel.margins = [10, 12, 10, 8];
        ui.unit = addLabeledField(sizePanel, "셀 크기 (px)", getSavedSetting("unit", "160"),
            "타일 한 칸의 기준 크기(컴프 픽셀). 1024/2 같은 계산식도 됩니다.");
        ui.gap = addLabeledField(sizePanel, "간격 (px)", getSavedSetting("gap", "8"),
            "이웃한 타일 사이 여백. (24-8)/2 같은 계산식도 됩니다.");
        ui.layoutWidth = addLabeledField(sizePanel, "최대 너비 (px)", getSavedSetting("width", "1920"),
            "그리드 전체의 최대 가로 폭. 컴프보다 크면 컴프 너비로 제한됩니다.");

        // 배치
        layoutPanel = palette.add("panel", undefined, "배치");
        layoutPanel.orientation = "column";
        layoutPanel.alignChildren = ["fill", "top"];
        layoutPanel.spacing = 4;
        layoutPanel.margins = [10, 12, 10, 8];

        g = layoutPanel.add("group"); g.orientation = "row"; g.alignChildren = ["left", "center"];
        label = g.add("statictext", undefined, "채우기"); label.preferredSize.width = 105;
        ui.fitMode = g.add("dropdownlist", undefined, ["채우기 (Cover) — 타일을 가득 채우고 남는 부분은 잘라냄", "맞추기 (Contain) — 이미지 전체가 보이게"]);
        savedFit = getSavedSetting("fit", "Cover");
        ui.fitMode.selection = savedFit === "Contain" ? 1 : 0;
        ui.fitMode.helpTip = "Cover 는 타일을 꽉 채우고 넘치는 부분을 마스크로 잘라냅니다. Contain 은 이미지를 자르지 않습니다.";

        g = layoutPanel.add("group"); g.orientation = "row"; g.alignChildren = ["left", "center"];
        label = g.add("statictext", undefined, "타일 변화"); label.preferredSize.width = 105;
        ui.tileVariety = g.add("dropdownlist", undefined, ["균형 — 최대 2×2", "대담 — 최대 3×3", "과감 — 최대 4×4"]);
        savedVariety = getSavedSetting("variety", "Balanced");
        ui.tileVariety.selection = savedVariety === "Wild" ? 2 : (savedVariety === "Bold" ? 1 : 0);
        ui.tileVariety.helpTip = "큰 타일을 얼마나 허용할지. 균형은 고르게, 대담·과감은 몇 개의 큰 대표 타일을 만듭니다.";

        g = layoutPanel.add("group"); g.orientation = "row"; g.alignChildren = ["left", "center"];
        label = g.add("statictext", undefined, "채우는 방식"); label.preferredSize.width = 105;
        ui.packingStyle = g.add("dropdownlist", undefined, ["정돈 — 규칙적으로 빈틈없이", "맞물림 (권장) — 크기를 섞어 긴 이음새를 끊음", "느슨한 모자이크 — 더 자유롭게 흩어 배치"]);
        savedStyle = getSavedSetting("packingStyle", "Interlocking");
        ui.packingStyle.selection = savedStyle === "Loose Mosaic" ? 2 : (savedStyle === "Compact" ? 0 : 1);
        ui.packingStyle.helpTip = "정돈은 질서 있게, 맞물림·느슨한 모자이크는 큰 타일의 위치와 크기를 섞어 세로로 분산합니다.";

        // 옵션
        optionPanel = palette.add("panel", undefined, "옵션");
        optionPanel.orientation = "column";
        optionPanel.alignChildren = ["left", "top"];
        optionPanel.spacing = 4;
        optionPanel.margins = [10, 12, 10, 8];
        ui.cropCover = optionPanel.add("checkbox", undefined, "채우기(Cover) 시 레이어 마스크로 잘라내기");
        ui.cropCover.value = getSavedSetting("crop", "1") !== "0";
        ui.cropCover.helpTip = "실행할 때마다 레이어마다 이름이 정해진 사각 마스크 하나를 만들거나 다시 계산합니다.";
        ui.centerLayout = optionPanel.add("checkbox", undefined, "그리드를 컴프 가운데에 놓기");
        ui.centerLayout.value = getSavedSetting("center", "1") !== "0";
        ui.mixOrientations = optionPanel.add("checkbox", undefined, "가로·세로 타일 섞기 (2×1 / 1×2)");
        ui.mixOrientations.value = getSavedSetting("mixOrientations", "1") !== "0";
        ui.mixOrientations.helpTip = "일부 타일을 가로형·세로형으로 바꿉니다(픽셀을 돌리지는 않음). Cover 에서는 그 이미지가 더 많이 잘릴 수 있습니다.";

        // 실행
        ui.repack = addAccentButton(palette, "▶  Bento Grid 적용", "같은 설정이면 항상 같은 결과로 배치하고 크롭 마스크를 다시 계산합니다.");
        buttons = palette.add("group");
        buttons.orientation = "row";
        buttons.alignChildren = ["fill", "center"];
        buttons.spacing = 4;
        ui.randomize = buttons.add("button", undefined, "무작위 배치");
        ui.randomize.helpTip = "큰 타일의 크기와 순서를 바꿔 다시 배치합니다.";
        ui.clearMasks = buttons.add("button", undefined, "Bento 마스크 제거");
        ui.clearMasks.helpTip = "선택한 레이어에서 " + CROP_MASK_NAME + " 마스크만 제거합니다. 크기·위치는 그대로, 실행 취소로 복원됩니다.";
        maskRow = palette.add("group");
        maskRow.orientation = "row";
        maskRow.alignChildren = ["fill", "center"];
        ui.deleteAllMasks = maskRow.add("button", undefined, "선택 레이어의 마스크 전부 삭제…");
        ui.deleteAllMasks.helpTip = "확인 후 선택한 레이어의 잠기지 않은 마스크를 모두 삭제합니다(직접 만든 마스크 포함).";

        ui.status = palette.add("statictext", undefined,
            "이미지 레이어를 선택한 뒤 [Bento Grid 적용] 또는 [무작위 배치]를 누르세요.", {multiline: true});
        ui.status.preferredSize.height = 32;
        ui.status.alignment = ["fill", "top"];
        ui.status.helpTip = "배치 후 건너뛴 레이어와 경고가 여기에 표시됩니다.";

        ui.repack.onClick = function () {
            runLayout(ui, false);
        };
        ui.randomize.onClick = function () {
            runLayout(ui, true);
        };
        ui.clearMasks.onClick = function () {
            clearBentoMasks(ui);
        };
        ui.deleteAllMasks.onClick = function () {
            deleteAllSelectedMasks(ui);
        };
        ui.fitMode.onChange = function () {
            ui.cropCover.enabled = ui.fitMode.selection && ui.fitMode.selection.index === 0;
        };
        ui.packingStyle.onChange = function () {
            ui.mixOrientations.enabled = ui.packingStyle.selection && ui.packingStyle.selection.index !== 0;
        };
        ui.cropCover.enabled = ui.fitMode.selection && ui.fitMode.selection.index === 0;
        ui.mixOrientations.enabled = ui.packingStyle.selection && ui.packingStyle.selection.index !== 0;

        palette.layout.layout(true);
        palette.layout.resize();
        palette.onResizing = palette.onResize = function () {
            this.layout.resize();
        };
        return palette;
    }

    // BANG_Toolbox: CEP 패널에서 $.evalFile 로 띄울 때 GC 로 창이 닫히지 않도록 전역에 보관하고, 이미 떠 있으면 앞으로 가져온다
    if ($.global.BANG_BentoPalette && $.global.BANG_BentoPalette instanceof Window) {
        try { $.global.BANG_BentoPalette.show(); $.global.BANG_BentoPalette.active = true; return; } catch (eShow) {}
    }
    var palette = buildUI(thisObj);
    if (palette && palette instanceof Window) {
        $.global.BANG_BentoPalette = palette;
        palette.onClose = function () { $.global.BANG_BentoPalette = null; };
        palette.center();
        palette.show();
    }
})(this);
