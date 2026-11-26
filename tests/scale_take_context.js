// Exercise the adviser used by the native UI and the one embedded in the WebView.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '..');
const assets = path.join(root, 'src/plugins/scale');
const html = fs.readFileSync(path.join(assets, 'ScaleWorkspace.html'), 'utf8');
const scripts = Array.from(html.matchAll(/<script>([\s\S]*?)<\/script>/g), match => match[1]);
const fixture = fs.readFileSync(path.join(__dirname, 'fixtures/ostf52/31-special-tracks-shared-core-track.ostf'), 'utf8');
for (const workspace of [false, true]) {
    const context = vm.createContext({});
    const sources = workspace ? scripts.filter(s => s.includes('function melodyPCs(') || s.includes('SA.compat={'))
                              : [fs.readFileSync(path.join(assets, 'Adviser.js'), 'utf8')];
    for (const script of sources) vm.runInContext(script, context);
    const api = context.SA;
    const doc = JSON.parse(fixture);
    // The two explicit vocal lanes share a neutral-named Core track. Its role
    // and the optional primary-melody shortcut do not identify it as a melody.
    delete doc.song.primaryMelodyTrackId;
    delete doc.song.melodyTrackIds;
    const t = doc.tracks[0];
    t.name = 'Lane 1';
    delete t.role;
    const n = { id:'accepted', start:0, duration:240, pitch:61 };
    t.clips = [{id:'out', start:0, notes:[n, {...n,id:'silent',pitch:65,probability:0}]},
               {id:'alternative', start:0, enabled:false, notes:[{...n,id:'alt',pitch:66}]}];
    const chord = {id:'ch',start:0,duration:480,label:'C'};
    doc.harmony.chords = [chord];
    const baseline = JSON.stringify(doc);
    const choices = api.candidates(doc,chord).items;
    assert.ok(choices.length > 0);
    assert.ok(choices.every(c => c.melCount === 1));
    assert.deepEqual([...api.melodyTrackIds(doc)], [t.id]);
    if (workspace) {
        const notes = api.compat.index(doc).notes;
        assert.deepEqual(Array.from(notes, n => [n.id,n.group]), [['accepted','melody']]);
    }
    assert.equal(JSON.stringify(doc),baseline);
    t.clips[0].enabled = false;
    t.clips[1].enabled = true;
    const next = api.candidates(doc,chord).items;
    assert.ok(next.every(c => c.melCount === 1));
    assert.ok(choices.some(c => c.melOutside.includes(1)) && next.some(c => c.melOutside.includes(6)));
}
console.log('Native adviser and WebView agree on shared vocal tracks and enabled take outputs.');
