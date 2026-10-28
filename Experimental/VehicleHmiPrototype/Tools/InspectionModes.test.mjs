import test from "node:test";
import assert from "node:assert/strict";
import { InspectionModes } from "../Source/InspectorPages.js";

test("display inspection list includes the cluster debug views with unique values", () =>
{
    const Values = InspectionModes.map(([, Value]) => Value);
    assert.equal(new Set(Values).size, Values.length, "inspection mode values must be unique");
    const Captions = new Map(InspectionModes.map(([Caption, Value]) => [Value, Caption]));
    assert.match(Captions.get(10), /visibility buffer/);
    assert.match(Captions.get(11), /deferred resolve/);
    assert.match(Captions.get(12), /Hi-Z/);
    assert.match(Captions.get(13), /glass layers/);
    assert.match(Captions.get(14), /LOD levels/);
    assert.equal(Captions.get(0), "Final image", "existing modes keep their values (frame record compatibility)");
    assert.equal(Captions.get(1), "Direct sun only");
    assert.equal(Captions.get(2), "Indirect diffuse (GI)");
});
