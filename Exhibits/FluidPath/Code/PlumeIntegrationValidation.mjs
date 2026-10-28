import assert from 'node:assert/strict';

let Activated = false;
let SourcesObserved = false;
let StepsObserved = false;
export default {
    BeforeFrame(Host) {
        if (!Activated) {
            Activated = true;
            Host.FixedStep = 1 / 60;
            Host.Settings.TestPlume = 1;
        }
        SourcesObserved ||= Host.Smoke.Sources.Count > 0;
        StepsObserved ||= Host.Smoke.StepsTaken > 0;
    },
    Report(Host) {
        assert.ok(Activated && SourcesObserved && StepsObserved);
        assert.ok(Host.Smoke.MarchGroups && Host.Smoke.Resident);
        console.log(`PASS active plume: ${Host.Smoke.StepsTaken} transport steps, sources and march resources present`);
    }
};
