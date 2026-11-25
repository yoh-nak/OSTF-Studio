// Native adapter for the supplied Max device's read-only analysis and audition modules.
// All writes to OSTF go through the Studio 5.0 schema-validated ScaleDocument.
let score, flow, currentChord, candidates, reference, pinned;
function openScore(bytes) {
    score = JSON.parse(bytes);
    flow = null;
    pinned = null;
}
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
