// Native adapter for the supplied Max device's read-only analysis and audition modules.
// All writes to OSTF go through the Studio 5.0 schema-validated ScaleDocument.
let score, flow, currentChord, candidates, reference, pinned;
function openScore(bytes) {
    score = JSON.parse(bytes);
    flow = null;
    pinned = null;
    // Store source tick intervals once per document, outside candidate selection.
    noteTracks = (score.tracks || []).filter(t => t.kind !== 'drum').map(t => ({
        name: t.name || t.id,
        notes: (t.clips || []).flatMap(c => (c.notes || []).filter(n =>
            Number.isInteger(n.pitch) && n.enabled !== false).map(n => ({
                start: c.start + n.start, end: c.start + n.start + n.duration, pitch: n.pitch
            }))).sort((a,b) => a.start-b.start)
    })).filter(t => t.notes.length);
}
let noteTracks = [];
function noteFit(index) {
    const scale = candidates[index];
    if (!scale || !currentChord) return '[]';
    const start = currentChord.start, end = start+currentChord.duration;
    return JSON.stringify(noteTracks.map(t => {
        let weight=0, inside=0;
        const outside = new Set();
        for (const n of t.notes) {
            if (n.start >= end) break;
            const overlap = Math.min(end,n.end)-Math.max(start,n.start);
            if (overlap<=0) continue;
            weight += overlap;
            if (scale.pcs.includes(n.pitch%12)) inside += overlap;
            else outside.add(n.pitch%12);
        }
        return {track:t.name, percent:weight ? Math.round(100*inside/weight) : -1,
                outside:[...outside].sort((a,b)=>a-b).map(pc=>SA.flow.spellScalePc(scale,pc)).join('  ')};
    }));
}
function compareNotes(index) {
    const b=candidates[index], a=pinned || reference;
    if (!a || !b) return '';
    const diff=SA.flow.differences(b,a);
    const pitches=list=>list.map(pc=>SA.flow.spellScalePc(b,pc)).join('  ') || '—';
    return 'A   '+SA.pitchName(a.tonic)+' '+a.name+'\nB   '+SA.pitchName(b.tonic)+' '+b.name
        +'\nShared   '+pitches(diff.common)+'\nB adds   '+pitches(diff.added)+'\nB omits   '+pitches(diff.removed);
}
function resetReference() { pinned=null; }
function genreStyles() { return JSON.stringify(SA.genre.styles); }
function rankChord(index, strategy, lens, genre = 'auto') {
    const chord = score.harmony.chords[index];
    const key = strategy + '/' + lens + '/' + genre;
    if (!flow || flow.key !== key)
        flow = {key, genre, plan: SA.flow.plan(score, 'harmony', strategy, true, lens, genre)};
    currentChord = chord;
    const analysis = SA.flow.ranked(score, chord, 'harmony', strategy, flow.plan, lens, genre);
    candidates = analysis.items;
    const assigned = SA.flow.savedInfo(score, chord, 'harmony', analysis);
    reference = assigned.scale || candidates[0];
    return JSON.stringify(candidates.map(s => ({
        name: SA.pitchName(s.tonic) + ' ' + s.name, root: s.root, intervals: s.intervals,
        pitches: s.pcs.map(pc => SA.flow.spellScalePc(s, pc)).join('   '),
        description: s.description, warnings: s.warnings.join(' '), reason: s.flowReason
    })));
}
function pinReference(index) { pinned = candidates[index] || null; }
function flowView(index) {
    const choice = candidates?.[index];
    if (!flow || !choice) return '{}';
    const plan = flow.plan, chords = plan.chords;
    const at = chords.findIndex(ch => ch.id === currentChord.id);
    const next = chords[at+1], nextScale = next && plan.chosen[next.id];
    return JSON.stringify({
        matrix: SA.flow.noteMatrix(score, chords, at, choice, 'harmony', plan),
        from: currentChord.label, to: next?.label || '',
        motions: SA.flow.transitions(currentChord, choice, next, nextScale)
    });
}
function listenTo(index, compare) {
    const choice = candidates[index];
    if (!choice || !reference) return '[]';
    const chords = flow.plan.chords, i = chords.findIndex(c => c.id === currentChord.id);
    const style = SA.genre.resolve(score, flow.genre).id;
    const pair = SA.audition.pair(score, currentChord, chords[i+1], pinned || reference, choice, chords[i-1], style);
    if (!pair.b) return '[]';
    if (!compare || !pair.a) return JSON.stringify(pair.b.events);
    return JSON.stringify(pair.a.events.concat(pair.b.events.map(e => ({...e, at:e.at + pair.a.duration + 400}))));
}
