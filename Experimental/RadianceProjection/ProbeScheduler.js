//============================================================================================================================================
//                                                          PROBESCHEDULER.JS
//============================================================================================================================================
// 📦 Budgeted probe refresh ordering with scene revisions, bounded settling and starvation prevention.

export class ProbeScheduler
{
    constructor(Count)
    {
        this.Records = Array.from({length:Count}, () => ({Remaining:0, Revision:-1, Captures:0, LastFrame:-1, DirtyFrame:0}));
        this.Revision = 0;
        this.Cursor = 0;
    }
    Invalidate(Frame, Iterations = 4)
    {
        ++this.Revision;
        this.Records.forEach(Record =>
        {
            if (Record.Remaining === 0) Record.DirtyFrame = Frame;
            Record.Remaining = Math.max(Record.Remaining, Iterations);
        });
    }
    Request(Index, Frame)
    {
        const Record = this.Records[Index];
        if (Record.Remaining === 0) Record.DirtyFrame = Frame;
        Record.Remaining = Math.max(Record.Remaining, 1);
    }
    Select(Budget, Frame, Weights, Prioritized = true)
    {
        Budget = Math.min(this.Records.length, Math.max(1, Math.floor(Budget)));
        if (!Prioritized)
        {
            const Selection = Array.from({length:Budget}, (_, Offset) => (this.Cursor + Offset) % this.Records.length);
            this.Cursor = (this.Cursor + Budget) % this.Records.length;
            return Selection;
        }
        const Deadline = 2 * Math.ceil(this.Records.length / Budget);
        return this.Records.map((Record, Index) =>
        {
            const Waiting = Frame - Math.max(Record.LastFrame, Record.DirtyFrame);
            // 📝 Fresh visibility precedes repeat bounce settling. Aging serves even off-camera probes.
            const Score = (Waiting >= Deadline ? 1000000 + Waiting * 1000 : 0) +
                (Record.Captures === 0 ? 100000 : 0) + (Record.Revision < this.Revision ? 10000 : 0) +
                (Weights[Index] || 0) + Waiting * 3;
            return {Index, Score, Pending:Record.Remaining > 0};
        }).filter(Entry => Entry.Pending).sort((Alpha, Beta) => Beta.Score - Alpha.Score || Alpha.Index - Beta.Index)
            .slice(0, Budget).map(Entry => Entry.Index);
    }
    Complete(Index, Frame)
    {
        const Record = this.Records[Index];
        Record.Remaining = Math.max(0, Record.Remaining - 1);
        Record.Revision = this.Revision;
        Record.LastFrame = Frame;
        ++Record.Captures;
    }
    QueryPending()
    {
        return this.Records.filter(Record => Record.Remaining > 0).length;
    }
}
