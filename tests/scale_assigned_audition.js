const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const html = fs.readFileSync(path.join(__dirname, '../src/plugins/scale/ScaleWorkspace.html'), 'utf8');
const page = vm.createContext({});
for (const [,script] of html.matchAll(/<script>([\s\S]*?)<\/script>/g)) {
    if (script.includes('function melodyPCs(') || script.includes('SA.assignedPreview=')) {
        vm.runInContext(script, page);
    }
}
const {assignedPreview, scope} = page.SA;
const section = {id:'second', name:'Second', start:480, duration:1920};
const scale = {id:'d', start:0, duration:960, tonic:{step:'D'}, intervals:[0,2,3,5,7,9,10]};
const next = {...scale, id:'f', start:1440, duration:1440, tonic:{step:'F'}};
const doc = {
    ppq:480, timeline:{duration:2880, sections:[section], tempoChanges:[{at:0,bpm:120},{at:1680,bpm:60}]},
    harmony:{chords:[], scales:[scale,next]}, tracks:[{id:'bass', directions:[
        {start:1440, duration:960, type:'improvisation', parameters:{pitchSource:{type:'harmonyScales',scaleIds:['f']}}},
        {start:2400, duration:240, type:'improvisation', parameters:{pitchSource:{type:'explicitScale',scale}}}
    ]}]
};
const unassigned={...doc,harmony:{chords:[{id:'example',start:480,duration:960}],scales:[]}};
const pristine=JSON.stringify(unassigned);
const candidatePlan=page.SA.auditionPlan(unassigned,'harmony',scope.range(unassigned,'section',null,null,section.id),()=>scale);
assert.equal(candidatePlan.suggested,true);
assert.equal(candidatePlan.notes[0].at,0);
assert.equal(candidatePlan.notes[0].note,62);
assert.equal(JSON.stringify(unassigned),pristine);
const original=JSON.stringify(doc);
const selection=scope.range(doc,'section',null,null,section.id);
const song=scope.range(doc,'song');
const compact = p => Array.from(p.notes, n => [n.at,n.note]);
const excerpt = assignedPreview(doc,'harmony',selection);
assert.deepEqual(Array.from(excerpt.cues, c=>[c.at,c.end]),[[0,500],[1000,2750]]);
assert.ok(excerpt.cues[0].label.includes('D'));
assert.ok(candidatePlan.cues[0].label.includes('D'));
assert.deepEqual(compact(excerpt),[[0,62],[250,64],[1000,65],[1250,67],[1750,68],[2250,70]]);
assert.equal(excerpt.duration,2750);
assert.deepEqual(compact(assignedPreview(doc,'bass',song)),
    [[1500,65],[1750,67],[2250,68],[2750,70],[3250,62]]);
assert.equal(JSON.stringify(doc),original);
assert.equal(assignedPreview(doc,'unknown',song).notes.length,0);
doc.harmony.scales.push({...scale,tonic:{step:'G'}});
assert.equal(assignedPreview(doc,'harmony',song).ambiguous,true);
assert.equal(assignedPreview(doc,'harmony',song).notes[0].at,1500);
doc.harmony.scales=[{...scale,duration:200000}];
doc.timeline.duration=200000;
const long=assignedPreview(doc,'harmony',scope.range(doc,'song'));
assert.ok(long.notes.at(-1).at>180000);
assert.ok(long.notes.every(n=>n.at+n.ms<=long.duration));
doc.song={scale, key:{tonic:{step:'C'},mode:'major'}};
doc.timeline.keyChanges=[{at:480,key:{tonic:{step:'G'},mode:'major'}}];
doc.tracks[0].directions=[
    {type:'improvisation',start:0,duration:960,parameters:{pitchSource:{type:'effectiveKeyScale'}}},
    {type:'improvisation',start:960,duration:240,parameters:{pitchSource:{type:'songScale'}}}
];
assert.deepEqual(compact(assignedPreview(doc,'bass',song)),[[0,60],[250,62],[500,67],[750,69],[1000,62]]);
doc.harmony.scales=[{...next,targets:[{type:'section',id:section.id}]}];
const withSongScale=assignedPreview(doc,'harmony',song);
assert.equal(withSongScale.ambiguous,false);
assert.equal(withSongScale.notes.find(n=>n.at===1500).note,65);
doc.tracks[0].directions=[];
doc.tracks[0].clips=[{id:'part',start:480,duration:960,notes:[{id:'held',start:240,duration:240}]}];
doc.harmony.scales=[{...scale,targets:[{type:'note',id:'held'}]}];
assert.deepEqual(compact(assignedPreview(doc,'bass',song)),[[750,62]]);
doc.tracks[0].clips[0].enabled=false;
assert.equal(assignedPreview(doc,'bass',song).notes.length,0);
console.log('Assigned scale audition respects the selected lane, range, tempo, gaps and long songs.');
