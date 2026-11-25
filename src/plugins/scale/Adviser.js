/* OSTF Scale Editor 0.1.0 — offline, non-destructive musical analysis. */
(function(root){
'use strict';
const mod=n=>((n%12)+12)%12, clone=x=>JSON.parse(JSON.stringify(x));
const natural={C:0,D:2,E:4,F:5,G:7,A:9,B:11};
const SH=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
const FL=['C','Db','D','Eb','E','F','Gb','G','Ab','A','Bb','B'];
const PC=x=>typeof x==='number'?mod(x):(x&&typeof x==='object'&&natural[x.step]!==undefined?mod(natural[x.step]+(x.alter||0)):parsePitch(x)?.pc??null);
function parsePitch(x){if(typeof x!=='string')return null; const m=x.replace(/♯/g,'#').replace(/♭/g,'b').match(/^([A-Ga-g])(bb|##|b|#|x)?$/);if(!m)return null;const step=m[1].toUpperCase(),a=m[2]||'',alter=a==='x'?2:a.split('').reduce((s,c)=>s+(c==='#'?1:-1),0);return {step,alter,pc:mod(natural[step]+alter)};}
function spelled(x){if(typeof x==='string'){const p=parsePitch(x);return p?{step:p.step,alter:p.alter}:null;}if(x&&natural[x.step]!==undefined)return {step:x.step,alter:x.alter||0};return null;}
function pitchName(x){if(x==null)return 'Unspecified';if(typeof x==='number')return SH[mod(x)];const p=spelled(x);return p?p.step+(p.alter>0?'#'.repeat(p.alter):'b'.repeat(-p.alter)):String(x);}
function spellDegree(tonic,degree){const p=spelled(tonic);if(!p)return '?';const m=String(degree).match(/^([b#]*)(\d+)$/);if(!m)return '?';const deg=+m[2],adj=m[1].split('').reduce((v,c)=>v+(c==='#'?1:-1),0),steps='CDEFGAB',idx=steps.indexOf(p.step),s=steps[(idx+deg-1)%7];const base=[0,2,4,5,7,9,11][(deg-1)%7],target=mod(PC(p)+base+adj);let alter=mod(target-natural[s]);if(alter>6)alter-=12;return Math.abs(alter)<=2?s+(alter>0?'#'.repeat(alter):'b'.repeat(-alter)):SH[target];}
const defs=[
['ionian','Ionian / Major','Inside',[0,2,4,5,7,9,11],['1','2','3','4','5','6','7'],'The major-key collection. The natural 11 can rub against a major third when sustained.'],
['dorian','Dorian','Inside',[0,2,3,5,7,9,10],['1','2','b3','4','5','6','b7'],'Minor seventh with a natural 6 / 13. Common on ii minor-seven in a major ii–V–I.'],
['phrygian','Phrygian','Color',[0,1,3,5,7,8,10],['1','b2','b3','4','5','b6','b7'],'Minor collection with a flat 2. The b9 needs particular care against a minor chord.'],
['lydian','Lydian','Color',[0,2,4,6,7,9,11],['1','2','3','#4','5','6','7'],'Major seventh with #11; often a useful color over IVmaj7 or explicitly raised 11.'],
['mixolydian','Mixolydian','Inside',[0,2,4,5,7,9,10],['1','2','3','4','5','6','b7'],'Unaltered dominant collection. Natural 11 may clash with the major third if emphasized.'],
['aeolian','Aeolian / Natural minor','Inside',[0,2,3,5,7,8,10],['1','2','b3','4','5','b6','b7'],'Natural minor. The flat 6 can rub against the fifth in sustained voicings.'],
['locrian','Locrian','Inside',[0,1,3,5,6,8,10],['1','b2','b3','4','b5','b6','b7'],'Half-diminished collection with b9. Common diatonic starting point for minor-key iiø7.'],
['melodic_minor','Melodic minor','Color',[0,2,3,5,7,9,11],['1','2','b3','4','5','6','7'],'Jazz melodic minor: same pitch collection up and down. Fits tonic minor-major-seven / minor-six colors.'],
['dorian_b2','Dorian b2','Color',[0,1,3,5,7,9,10],['1','b2','b3','4','5','6','b7'],'Minor seventh with b9 and natural 13; a deliberate color, not a universal minor-seven default.'],
['lydian_augmented','Lydian augmented','Color',[0,2,4,6,8,9,11],['1','2','3','#4','#5','6','7'],'Major seventh with #5 and #11. Check any natural fifth in the accompaniment.'],
['lydian_dominant','Lydian dominant','Color',[0,2,4,6,7,9,10],['1','2','3','#4','5','6','b7'],'Dominant with natural 9, #11 and 13. Especially useful for many non-resolving or tritone-substitute dominants.'],
['mixolydian_b6','Mixolydian b6','Color',[0,2,4,5,7,8,10],['1','2','3','4','5','b6','b7'],'Dominant with natural 9 and b13. Check the natural 11 against the third.'],
['locrian_2','Locrian natural 2','Color',[0,2,3,5,6,8,10],['1','2','b3','4','b5','b6','b7'],'Half-diminished with natural 9. A common alternative to Locrian when the melody permits.'],
['altered','Altered','Tension',[0,1,3,4,6,8,10],['1','b9','#9','3','#11','b13','b7'],'Altered dominant vocabulary. Natural 5, 9 and 13 are absent. Resolve the altered tones intentionally; check comping.'],
['harmonic_minor','Harmonic minor','Color',[0,2,3,5,7,8,11],['1','2','b3','4','5','b6','7'],'Minor tonic with a leading tone and b6. Check natural 6 or b7 in an existing arrangement.'],
['phrygian_dominant','Phrygian dominant','Tension',[0,1,4,5,7,8,10],['1','b2','3','4','5','b6','b7'],'Fifth mode of harmonic minor: dominant b9 / b13, especially before a minor tonic.'],
['half_whole','Half–whole diminished','Tension',[0,1,3,4,6,7,9,10],['1','b9','#9','3','#11','5','13','b7'],'Dominant diminished collection with b9, #9, #11 and natural 13. Not interchangeable with Altered when 13 is fixed.'],
['whole_half','Whole–half diminished','Tension',[0,2,3,5,6,8,9,11],['1','2','b3','4','b5','b6','6','7'],'Symmetric diminished-seven collection. The sixth pitch class is also the diminished seventh (bb7).'],
['whole_tone','Whole tone','Tension',[0,2,4,6,8,10],['1','2','3','#4','#5','b7'],'Symmetric dominant / augmented color. No natural fifth; check altered versus unaltered tensions.'],
['major_pent','Major pentatonic','Inside',[0,2,4,7,9],['1','2','3','5','6'],'Five-note major vocabulary. Omits the seventh; it does not describe the full harmony.'],
['minor_pent','Minor pentatonic','Inside',[0,3,5,7,10],['1','b3','4','5','b7'],'Five-note minor vocabulary. A useful subset, not a complete chord-scale description.'],
['blues','Minor blues','Color',[0,3,5,6,7,10],['1','b3','4','b5','5','b7'],'Minor-pentatonic vocabulary plus the blue fifth. Over a major third, b3 is an expressive tension, not a consonance guarantee.'],
['bebop_dominant','Dominant bebop','Color',[0,2,4,5,7,9,10,11],['1','2','3','4','5','6','b7','7'],'Mixolydian plus a passing major seventh. The added tone is rhythmic passing material, not a sustained dominant chord tone.'],
['bebop_major','Major bebop','Color',[0,2,4,5,7,8,9,11],['1','2','3','4','5','#5','6','7'],'Major collection plus a passing #5. Note placement matters; selecting a collection does not generate bebop phrasing.']
].map(([id,name,group,intervals,degrees,description])=>({id,name,group,intervals,degrees,description}));
const scales=Object.fromEntries(defs.map(x=>[x.id,x]));
const MODES={major:'ionian',ionian:'ionian',minor:'aeolian',aeolian:'aeolian',dorian:'dorian',phrygian:'phrygian',lydian:'lydian',mixolydian:'mixolydian',locrian:'locrian','melodic minor':'melodic_minor','harmonic minor':'harmonic_minor'};
function parseChord(c){
 const label=String(c.label||''),s=label.trim().replace(/♭/g,'b').replace(/♯/g,'#').replace(/[Δ△]/g,'maj').replace(/−/g,'-');
 let tonic=spelled(c.root),rootPC=PC(tonic),ints=Array.isArray(c.intervals)?[...new Set(c.intervals.map(mod))].sort((a,b)=>a-b):null;
 const m=s.match(/^([A-Ga-g])([#b]{1,2})?(.*)$/);
 if(!tonic&&m){tonic=spelled(m[1].toUpperCase()+(m[2]||''));rootPC=PC(tonic);}
 if(rootPC===null)return {label,supported:false,reason:'No unambiguous chord root. Chord preserved, analysis skipped.'};
 let q=m?m[3].replace(/\s/g,'').replace(/Maj/g,'maj').replace(/^mM/,'mmaj'):'';const slash=q.match(/\/([A-G][#b]{0,2})$/i);if(slash)q=q.slice(0,-slash[0].length);
 let authored=!!ints,explicit=[],alt=/alt/i.test(q);
 if(!ints){
  let t=q.replace(/\([^)]+\)/g,x=>x.slice(1,-1)).replace(/,/g,'');
  const minor=/^(m(?!aj)|min|-)/.test(t),dim=/^(dim|o|°)/.test(t),hd=/^(ø|m7b5|min7b5|-7b5)/.test(t),aug=/^(aug|\+)/.test(t),sus4=/sus(?:4)?/.test(t),sus2=/sus2/.test(t),maj=/(maj|^M)/.test(t);
  const rest=t.replace(/^(maj|min|dim|aug|m|M|-|o|°|ø|\+)/,'').replace(/maj/g,'').replace(/sus(?:2|4)?/g,'').replace(/(?:add|omit|no)[#b]?\d+/g,'').replace(/[#b](?:5|9|11|13)/g,'').replace(/(?:6\/9|69|13|11|9|7|6|5)/g,'').replace(/alt/g,'');
  if(rest)return {label,root:rootPC,tonic,supported:false,reason:'Unsupported or ambiguous chord suffix: '+q+'. Add explicit root and intervals in OSTF TEXT.'};
  ints=dim||hd?[0,3,6]:aug?[0,4,8]:minor?[0,3,7]:sus2?[0,2,7]:sus4?[0,5,7]:[0,4,7];
  if(q==='5')ints=[0,7];
  const sixNine=/(?:6\/9|69)/.test(t),bare=t.replace(/(?:add|omit|no)[#b]?\d+/g,'').replace(/[#b](?:5|9|11|13)/g,'');
  if(hd)ints.push(10);else if(!sixNine&&/7|9|11|13/.test(bare)){ints.push(dim?9:maj?11:10);}else if(/6/.test(t)){ints.push(9);explicit.push(9);}
  if(sixNine){ints.push(2);explicit.push(2,9);}
  if(/(?:maj7|M7)/.test(t)&&!ints.includes(11))ints.push(11);
  if(/(?:^|[^b#\d])(?:9|11|13)/.test(t)){ints.push(2);explicit.push(2);}
  if(/(?:^|[^b#\d])11/.test(t)){ints.push(5);explicit.push(5);}
  if(/(?:^|[^b#\d])13/.test(t)){ints.push(9);explicit.push(9);}
  for(const x of t.matchAll(/(add)?([b#]?)(5|9|11|13)/g)){
   if(!x[1]&&!x[2])continue;let orig={5:7,9:2,11:5,13:9}[x[3]],v=mod(orig+(x[2]==='b'?-1:x[2]==='#'?1:0));
   if(x[2])ints=ints.filter(n=>n!==orig);ints.push(v);explicit.push(v);
  }
  if(/(?:no|omit)3/.test(t))ints=ints.filter(n=>n!==3&&n!==4);if(/(?:no|omit)5/.test(t))ints=ints.filter(n=>n!==7);
  if(alt){ints=[0,4,10];explicit=[];}
  ints=[...new Set(ints)].sort((a,b)=>a-b);
 }
 const has=n=>ints.includes(n);
 let family=has(3)&&has(6)&&has(9)&&!has(10)?'dim':has(3)&&has(6)&&has(10)?'halfDim':has(3)&&has(6)&&!has(4)?'dimTriad':has(4)&&has(10)?'dominant':has(4)&&has(11)?(has(8)?'majAug':'major7'):has(3)&&has(11)?'minorMajor':has(3)?'minor':!has(3)&&!has(4)&&has(5)?'sus':has(4)&&has(8)?'aug':has(4)?'major':'other';
 let essential=ints.filter(n=>[0,3,4,10,11].includes(n));
 if(['halfDim','dim','dimTriad'].includes(family))essential=ints.filter(n=>n!==2&&n!==5&&n!==8&&n!==11);
 if(['aug','majAug'].includes(family))essential.push(8);if(family==='sus')essential=[0,5,...(has(10)?[10]:[])];
 if(authored){explicit=ints.filter(n=>![0,3,4,7,10,11].includes(n));if(family==='minor'&&has(9))explicit.push(9);}
 return {label,root:rootPC,tonic,intervals:ints,pcs:ints.map(n=>mod(rootPC+n)),family,essential:[...new Set(essential)],explicit:[...new Set(explicit)],alt,supported:true,authored};
}
function effectiveKey(doc,tick){let key=doc.song?.key;for(const c of [...(doc.timeline?.keyChanges||[])].sort((a,b)=>a.at-b.at)){if(c.at<=tick)key=c.key;else break;}if(!key||key.state==='atonal'||key.state==='unspecified')return null;let k=typeof key==='string'?key.match(/^([A-G][#b]?)\s*(.*)$/i):null;let tonic=k?spelled(k[1]):spelled(key.tonic),mode=k?(k[2]||'major').toLowerCase():String(key.mode||'').toLowerCase();let sid=MODES[mode];if(!tonic||!sid)return null;return {tonic,mode,root:PC(tonic),pcs:scales[sid].intervals.map(n=>mod(PC(tonic)+n))};}
function labelKey(doc,tick){const k=effectiveKey(doc,tick);if(k)return pitchName(k.tonic)+' '+k.mode;let key=doc.song?.key;for(const c of (doc.timeline?.keyChanges||[])){if(c.at<=tick)key=c.key;}return key?.label||key?.state||'Key unspecified';}

/* Music-style evidence v0.24. A twelve-bar-like I7/IV7/V7 pattern is
 * a *hypothesis*, not proof that the user's song is a blues. No key, chords,
 * or OSTF genre metadata are modified. This is computed once per document. */
const styleCache = typeof WeakMap==='function'?new WeakMap():null;
function authoredBluesTag(doc){
 const vals=[doc?.song?.genre,doc?.song?.genres,doc?.metadata?.genre,doc?.metadata?.genres];
 const flat=vals.flatMap(x=>Array.isArray(x)?x:[x]).filter(x=>typeof x==='string').map(x=>x.toLowerCase());
 return flat.some(x=>/(^|[^a-z])blues([^a-z]|$)/.test(x));
}
function dominantBluesShape(doc){
 if(styleCache&&styleCache.has(doc))return styleCache.get(doc);
 const chords=doc?.harmony?.chords||[], n=chords.length;
 let result={detected:false,relevantRoots:[],tonic:null,confidence:'unknown',description:'No dominant-blues evidence'};
 if(n>=8){
  const parsed=chords.map(parseChord),dom=parsed.filter(x=>x.supported&&x.family==='dominant');
  if(dom.length/n>=.72){
   const authored=effectiveKey(doc,chords[0]?.start||0), candidates=[];
   // Consider tonic of the first chord when no authored key is available; never
   // infer a global key solely because these pitch classes fit I/IV/V motion.
   const roots=[...new Set([authored?.root,parsed[0]?.root].filter(Number.isInteger))];
   for(const tonic of roots){
    const allowed=new Set([tonic,mod(tonic+5),mod(tonic+7)]);
    const matching=dom.filter(c=>allowed.has(c.root)).length;
    const kinds=new Set(dom.filter(c=>allowed.has(c.root)).map(c=>mod(c.root-tonic)));
    if(kinds.size>=2&&matching/dom.length>=.85){candidates.push({tonic,matching,kinds,score:matching+2*kinds.size+(authored?.root===tonic?1:0)});}
   }
   candidates.sort((a,b)=>b.score-a.score);
   if(candidates.length){
    const x=candidates[0],high=x.kinds.size===3&&x.matching/dom.length>=.90;
    result={detected:true,relevantRoots:[x.tonic,mod(x.tonic+5),mod(x.tonic+7)],tonic:x.tonic,
      confidence:high?'moderate':'low',description:'Repeated I7 / IV7 / V7 dominant pattern; blues-style hypothesis, not a genre assertion',
      authoredTag:authoredBluesTag(doc)};
   }
  }
 }
 if(styleCache)styleCache.set(doc,result);
 return result;
}

/* Key hypothesis is advisory only: song.key and keyChanges are never edited. */
function estimateKey(doc,chord){
 if(effectiveKey(doc,chord.start))return null;
 const all=[...(doc.harmony?.chords||[])].sort((a,b)=>a.start-b.start);
 const idx=all.findIndex(c=>c.id===chord.id);
 if(idx<0)return null;
 // Restrict an unauthored local-key hypothesis to the current authored
 // section. Otherwise three chords spanning a verse/chorus change can invent
 // a stronger *global* center than the section supports.
 const section=(doc.timeline?.sections||[]).filter(x=>x.start<=chord.start&&chord.start<x.start+x.duration).sort((a,b)=>b.start-a.start||a.duration-b.duration)[0]||null;
 const window=all.slice(Math.max(0,idx-4),Math.min(all.length,idx+5));
 const usable=window.map((v,j)=>({raw:v,c:parseChord(v),weight:Math.abs((Math.max(0,idx-4)+j)-idx)<=2?1.3:1})).filter(x=>
  (!section||(x.raw.start>=section.start&&x.raw.start<section.start+section.duration))&&
  x.c.supported&&(x.c.intervals.includes(3)||x.c.intervals.includes(4)));
 if(usable.length<3||new Set(usable.map(x=>x.c.root)).size<3)return null;
 const designs=[
  {mode:'major',degrees:{0:'major',2:'minor',4:'minor',5:'major',7:'major',9:'minor',11:'dim'},pcs:scales.ionian.intervals},
  {mode:'minor',degrees:{0:'minor',2:'dim',3:'major',5:'minor',7:'major-or-minor',8:'major',10:'major'},pcs:scales.aeolian.intervals}
 ];
 const options=[];
 for(const d of designs)for(let root=0;root<12;root++){
  let score=0,considered=0;
  for(const entry of usable){
   const c=entry.c,w=entry.weight,degree=mod(c.root-root),expected=d.degrees[degree];
   if(!expected){score-=2*w;continue;}
   considered++;
   const majorThird=c.intervals.includes(4),minorThird=c.intervals.includes(3);
   const fits=expected==='major'?majorThird&&!minorThird:expected==='minor'?minorThird&&!majorThird:expected==='dim'?minorThird&&c.intervals.includes(6):majorThird||minorThird;
   score+=(fits?3.2:-2.3)*w;
   // A genuine tonic arrival is relevant, but not sufficient alone to infer a key.
   if(degree===0&&fits){score+=1.1*w;if(entry.raw.id===chord.id)score+=1.8;}
   if(c.family==='dominant'&&degree===7)score+=1.1*w;
  }
  options.push({mode:d.mode,root,score,considered,pcs:d.pcs.map(n=>mod(root+n))});
 }
 options.sort((a,b)=>b.score-a.score);
 const best=options[0],second=options[1],margin=best.score-second.score;
 if(best.score<7||best.considered<3)return null;
 const sharp=['C','C#','D','Eb','E','F','F#','G','Ab','A','Bb','B'];
 return {...best,tonic:spelled(sharp[best.root]),name:sharp[best.root]+' '+best.mode,margin,confidence:margin>=2.8?'moderate':'low',alternatives:options.slice(1,3).map(x=>sharp[x.root]+' '+x.mode)};
}

function context(doc,chord){const sorted=[...doc.harmony.chords].sort((a,b)=>a.start-b.start),idx=sorted.findIndex(x=>x.id===chord.id),cur=parseChord(chord),next=idx>=0?sorted[idx+1]:null,previous=sorted[idx-1]||null,n=next?parseChord(next):null,key=effectiveKey(doc,chord.start),reasons=[];
 const estimatedKey=key?null:estimateKey(doc,chord);
 const keyBoundary=(doc.timeline?.keyChanges||[]).filter(x=>Number.isFinite(x.at)&&x.at>chord.start&&x.at<chord.start+chord.duration).map(x=>x.at).sort((a,b)=>a-b);
 const adjacent=n?.supported&&chord.start+chord.duration===next.start;
 let cadence=adjacent&&cur.supported&&cur.family==='dominant'&&mod(cur.root-n.root)===7;
 let sub=adjacent&&cur.supported&&cur.family==='dominant'&&mod(cur.root-n.root)===1;
 const blues=dominantBluesShape(doc);const bluesRelevant=blues.detected&&cur.family==='dominant'&&blues.relevantRoots.includes(cur.root);
 let minorResolve=cadence&&['minor','minorMajor','halfDim'].includes(n.family);
 let roman='?';if(cur.supported&&key){let di=key.pcs.indexOf(cur.root);roman=di<0?'Chromatic':['I','II','III','IV','V','VI','VII'][di];if(['minor','minorMajor','halfDim','dim'].includes(cur.family))roman=roman.toLowerCase();if(cur.family==='halfDim')roman+='ø';else if(cur.family==='dim')roman+='°';if(cur.intervals.includes(10)||cur.intervals.includes(11))roman+='7';}
 if(cadence){reasons.push('Dominant root motion to '+next.label+'; '+(minorResolve?'minor':'major / non-minor')+' resolution.');}
 else if(sub)reasons.push('Semitone-down dominant motion: possible tritone substitution; not a definitive analysis.');
 else if(cur.family==='dominant')reasons.push('No immediate fifth-down resolution found; a color dominant is possible.');
 if(key)reasons.push('Effective key: '+pitchName(key.tonic)+' '+key.mode+'.');else if(estimatedKey)reasons.push('Key not authored. Local hypothesis: '+estimatedKey.name+' ('+estimatedKey.confidence+' confidence); not written to OSTF.');else reasons.push('Key unresolved. Suggestions use the chord and its immediate neighbors only.');
 if(bluesRelevant)reasons.push('Dominant-blues pattern: Mixolydian is a common harmonic baseline; minor-blues notes remain expressive phrase options.');
 if(previous)reasons.push('Previous chord: '+previous.label+'.');
 if(keyBoundary.length)reasons.push('Authored key change inside this chord range. One scale for the entire chord may cross distinct tonal contexts; bulk assignment requires review.');
 return {cur,next:n?.supported?n:null,nextEvent:next,previous,key,keyBoundary,estimatedKey,blues:bluesRelevant?blues:null,cadence,sub,minorResolve,roman,reasons};}
function melodyPCs(doc,chord){const ids=new Set(doc.song?.melodyTrackIds||[]);if(doc.song?.primaryMelodyTrackId)ids.add(doc.song.primaryMelodyTrackId);const out=new Set();let count=0;for(const t of doc.tracks||[]){if(t.enabled===false||t.trackType==='drum')continue;if(!ids.has(t.id)&&!['melody','vocal'].includes(t.role))continue;for(const cl of t.clips||[]){if(cl.enabled===false)continue;for(const n of cl.notes||[]){if(n.enabled===false||n.pitchRole==='fixedTrigger'||!Number.isFinite(n.pitch))continue;const at=cl.start+n.start;if(at<chord.start+chord.duration&&at+n.duration>chord.start){out.add(mod(n.pitch));count++;}}}}return {pcs:[...out],count};}
function candidates(doc,chord){const cx=context(doc,chord),c=cx.cur;if(!c.supported)return {context:cx,items:[]};
 let ids={major:['ionian','lydian','mixolydian','major_pent','bebop_major'],major7:['ionian','lydian','major_pent','bebop_major'],majAug:['lydian_augmented','lydian'],minor:['dorian','aeolian','phrygian','minor_pent','blues','melodic_minor','harmonic_minor','dorian_b2'],minorMajor:['melodic_minor','harmonic_minor'],dominant:['mixolydian','lydian_dominant','altered','half_whole','phrygian_dominant','mixolydian_b6','whole_tone','bebop_dominant','blues'],halfDim:['locrian','locrian_2'],dim:['whole_half'],dimTriad:['whole_half','locrian','locrian_2'],sus:['mixolydian','dorian','phrygian'],aug:['whole_tone','lydian_augmented'],other:['ionian','aeolian','dorian','mixolydian','phrygian','major_pent','minor_pent','blues']}[c.family]||[];
 const mel=melodyPCs(doc,chord);
 const hint=cx.estimatedKey?.confidence==='moderate'?cx.estimatedKey:null;
 const choices=ids.map(id=>({id,base:scales[id],root:c.root,tonic:c.tonic}));
 // On a fully authored effective key/mode, a melody or synth riff can stay in
 // the same SONG-TONIC pitch collection across changes of chord root. This is
 // separate from a chord-root mode: e.g. A Aeolian on Am-F-C-G stays A Aeolian,
 // not F Ionian / C Ionian / G Mixolydian. Never fabricate it from a weak
 // key estimate or a genre name, and do not write it as shared Harmony.
 if(cx.key && cx.key.root!==c.root && scales[MODES[cx.key.mode]]){
  const sid=MODES[cx.key.mode];
  choices.push({id:'song_mode',base:scales[sid],root:cx.key.root,tonic:cx.key.tonic});
 }
 // A stable I7/IV7/V7 blues often supports *one song-tonic* minor-blues
 // vocabulary across changes. Its pitches MUST NOT be transposed to IV/V.
 // It is a phrase option, not a replacement for each chord's major third.
 if(cx.blues&&cx.blues.tonic!==c.root&&c.family==='dominant'){
  const root=cx.blues.tonic;
  choices.push({id:'blues_tonic',base:scales.blues,root,tonic:spelled(SH[root])});
 }
 const items=choices.map(({id,base,root:scaleRoot,tonic:scaleTonic},order)=>{
  const s=id==='blues_tonic'?{...base,id,name:'Minor blues · song tonic',description:'Song-tonic minor blues across I7, IV7 and V7. Expressive cross-chord phrase material, not a complete IV/V chord scale.'}:
    id==='song_mode'?{...base,id,name:base.name+' · song tonic',description:'Authored key/mode retained across chord roots. You can manually SET this pitch collection on a selected chord in Harmony; review borrowed or chromatic notes. AUTO-SET does not silently do this.'}:base;
  const pcs=s.intervals.map(x=>mod(scaleRoot+x));
  // The candidate and the chord may have *different tonics*, therefore
  // always compare absolute pitch classes, not relative interval numbers.
  const missing=c.essential.filter(x=>!pcs.includes(mod(c.root+x)));
  const extra=c.explicit.filter(x=>!pcs.includes(mod(c.root+x)));
  const keyOutside=cx.key?pcs.filter(x=>!cx.key.pcs.includes(x)):[],melOutside=mel.pcs.filter(x=>!pcs.includes(x));
  let score=60-order,notes=[],warnings=[];
  score-=missing.length*27+extra.length*13+melOutside.length*10;if(id.endsWith('_pent'))score-=8;if(cx.key)score+=Math.max(0,7-keyOutside.length)*3;
  if(hint){const outside=pcs.filter(x=>!hint.pcs.includes(x)).length;score+=(7-outside)*1.7;}
  if(cx.cadence&&id==='mixolydian'&&!cx.minorResolve)score+=14;
  if(cx.cadence&&id==='altered')score+=10;
  if(cx.minorResolve&&id==='phrygian_dominant')score+=25;
  if((cx.sub||c.family==='dominant'&&!cx.cadence&&!cx.key&&!cx.blues)&&id==='lydian_dominant')score+=16;
  if(cx.blues&&id==='mixolydian')score+=25;
  if(c.alt&&id==='altered')score+=45;
  if(c.family==='minor'&&c.intervals.includes(9)&&!c.intervals.includes(10)&&id==='melodic_minor')score+=18;
  if(c.family==='minor'&&cx.key&&mod(c.root-cx.key.root)===2&&cx.key.mode==='major'&&id==='dorian')score+=15;
  if(c.family==='major7'&&cx.key&&mod(c.root-cx.key.root)===5&&id==='lydian')score+=15;
  if(missing.length)warnings.push('Missing chord tones: '+missing.map(n=>SH[mod(c.root+n)]).join(', ')+'.');
  if(extra.length)warnings.push('Check specified color tones: '+extra.map(n=>SH[mod(c.root+n)]).join(', ')+'.');
  if(c.intervals.includes(7)&&!pcs.includes(mod(c.root+7)))warnings.push('Natural fifth '+SH[mod(c.root+7)]+' is absent; check the accompaniment.');
  if(c.intervals.includes(4)&&pcs.includes(mod(c.root+5)))warnings.push('Natural 11 is a sustain / voicing caution, not a forbidden passing note.');
  if(id.startsWith('bebop'))warnings.push('Added chromatic note is a passing-tone option; placement is not analyzed.');
  if((id==='blues'||id==='blues_tonic')&&c.family==='dominant')warnings.push('Minor-blues third against a dominant major third is an expressive tension; do not treat as consonant full harmony.');
  if(id==='blues_tonic')notes.push('Song tonic: '+SH[scaleRoot]+'. This minor-blues palette can pass through I7 / IV7 / V7. Manual Harmony editing is possible with a chord-tone warning; AUTO-SET still restricts it to Performance.');
  if(id==='song_mode'){
   notes.push('Authored song-key mode: '+pitchName(scaleTonic)+' '+cx.key.mode+'. Explicit SET SCALE can write this pitch collection to Harmony; automatic whole-song Harmony placement remains conservative.');
   if(missing.length||extra.length)warnings.push('Borrowed/chromatic chord outside the authored mode: required chord tones are missing. No automatic song-mode assignment, even with expressive opt-in.');
  }
  if(cx.blues&&id==='mixolydian')notes.push('Dominant-blues pattern: Mixolydian is a conventional baseline, not the only valid solo vocabulary.');
  if(melOutside.length)warnings.push('Authored melody pitch classes outside: '+melOutside.map(x=>FL[x]).join(', ')+'. Timing / emphasis are not judged.');
  if(!missing.length)notes.push('All essential chord tones present.');
  else if(id.endsWith('_pent'))notes.push('Reduced pitch vocabulary; omissions are not automatically errors.');
  if(cx.key&&!keyOutside.length)notes.push('Same pitch-class collection or subset of the effective key.');
  if(hint)notes.push('Locally inferred '+hint.name+' context is advisory; no global key is assumed.');
  if(id==='altered'&&cx.cadence)notes.push('Altered tensions can lead into '+cx.next.label+'.');
  if(id==='lydian_dominant'&&cx.sub)notes.push('Fits a common #11 tritone-substitute color.');
  if(id==='phrygian_dominant'&&cx.minorResolve)notes.push('Shares the target minor harmonic-minor collection.');
  return {...s,tonic:scaleTonic,root:scaleRoot,pcs,score,notes,warnings,missing,extra,keyOutside,melOutside,melCount:mel.count,fit:missing.length||extra.length?'Check voicing':'Core fit'};
 }).sort((a,b)=>b.score-a.score);
 return {context:cx,items};}
function validate(doc){const errors=[],warnings=[];const obj=x=>x&&typeof x==='object'&&!Array.isArray(x),integer=n=>Number.isSafeInteger(n)&&n>=0;
 if(!obj(doc))return {ok:false,errors:['Document root must be an object.'],warnings:[],writable:false};
 if(doc.format!=='OSTF')errors.push('format must be "OSTF".');if(typeof doc.version!=='string')errors.push('version must be a string.');
 if(!Number.isSafeInteger(doc.ppq)||doc.ppq<1||doc.ppq>960000)errors.push('ppq must be an integer from 1 to 960000.');
 if(!obj(doc.song)||typeof doc.song.title!=='string')errors.push('song.title is required.');
 if(!obj(doc.timeline))errors.push('timeline is required.');else{for(const k of ['sections','tempoChanges','meterChanges'])if(!Array.isArray(doc.timeline[k]))errors.push('timeline.'+k+' must be an array.');
 for(const x of doc.timeline.sections||[])if(!integer(x.start)||!Number.isSafeInteger(x.duration)||x.duration<=0||typeof x.id!=='string'||typeof x.name!=='string')errors.push('Invalid Section: '+(x.id||'?'));
 for(const x of doc.timeline.tempoChanges||[])if(!integer(x.at)||typeof x.bpm!=='number'||x.bpm<=0||x.bpm>1000)errors.push('Invalid tempo event.');
 for(const x of doc.timeline.meterChanges||[])if(!integer(x.at)||!Number.isSafeInteger(x.numerator)||x.numerator<1||![1,2,4,8,16,32,64].includes(x.denominator))errors.push('Invalid meter event.');
 if(!doc.timeline.tempoChanges?.length||doc.timeline.tempoChanges[0].at!==0)errors.push('An initial tempo at tick 0 is required.');if(!doc.timeline.meterChanges?.length||doc.timeline.meterChanges[0].at!==0)errors.push('An initial meter at tick 0 is required.');}
 if(!obj(doc.harmony)||!Array.isArray(doc.harmony.chords)||!Array.isArray(doc.harmony.scales))errors.push('harmony.chords and harmony.scales must be arrays.');
 for(const k of ['tracks','soundSlots','resources'])if(!Array.isArray(doc[k]))errors.push(k+' must be an array.');if(!obj(doc.lyrics))errors.push('lyrics must be an object.');
 const idOK=id=>typeof id==='string'&&/^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$/.test(id);
 const isSp=x=>obj(x)&&Object.hasOwn(natural,x.step)&&Number.isInteger(x.alter)&&Math.abs(x.alter)<=2;
 const isIntervals=(x,max)=>Array.isArray(x)&&x.length>0&&x.every(i=>Number.isInteger(i)&&i>=0&&i<=max)&&new Set(x).size===x.length&&x.includes(0);
 for(const c of doc.harmony?.chords||[]){if(!idOK(c.id)||!integer(c.start)||!Number.isSafeInteger(c.duration)||c.duration<=0||typeof c.label!=='string'||!c.label.length)errors.push('Invalid chord timing, ID or label: '+(c.id||'?'));if((c.root&&!c.intervals)||(!c.root&&c.intervals))errors.push('Chord root and intervals must be supplied together: '+c.id);if(c.root&&(!isSp(c.root)||!isIntervals(c.intervals,35)))errors.push('Invalid chord pitch data: '+c.id);}
 for(const s of doc.harmony?.scales||[])if(!idOK(s.id)||!integer(s.start)||!Number.isSafeInteger(s.duration)||s.duration<=0||!isSp(s.tonic)||!isIntervals(s.intervals,11))errors.push('Invalid harmony scale: '+(s.id||'?'));
 const seen=new Set();for(const arr of [doc.timeline?.sections,doc.timeline?.keyChanges,doc.harmony?.chords,doc.harmony?.scales,doc.tracks,doc.soundSlots,doc.resources])for(const x of arr||[]){if(x.id){if(seen.has(x.id))errors.push('Duplicate core ID: '+x.id);seen.add(x.id);}}
 const version=String(doc.version||''),recognized=/^(3\.[0-5](?:\.\d+)?|4\.[01](?:\.\d+)?)$/.test(version);
 if(!recognized)warnings.push('Unrecognized OSTF version '+version+': preserved read-only. No automatic downgrade or migration.');
 if(doc.harmony?.chords?.length===0)warnings.push('No chord events. Import a song with harmony.chords to receive suggestions.');
 for(const t of doc.tracks||[]){const ds=(t.directions||[]).filter(d=>d.type==='improvisation').sort((a,b)=>a.start-b.start);for(let i=1;i<ds.length;i++)if(ds[i].start<ds[i-1].start+ds[i-1].duration)warnings.push('Overlapping improvisation directions on '+(t.name||t.id)+'.');}
 warnings.push('Prototype validation checks the core structure used here; it is not a full OSTF conformance validator.');
 return {ok:!errors.length,errors:[...new Set(errors)],warnings:[...new Set(warnings)],writable:recognized};}
function parse(text){if(typeof text!=='string'||text.length>24*1024*1024)throw Error('Input exceeds 24 MB or is not text.');let t=text.replace(/^\uFEFF/,'').trim();if(/^```/.test(t))t=t.replace(/^```[^\n]*\n/,'').replace(/\n```\s*$/,'');let d;const nums=t.replace(/"(?:\\.|[^"\\])*"/g,'""').match(/-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?/g)||[];if(nums.some(x=>{const n=Number(x);return !Number.isFinite(n)||(Number.isInteger(n)&&!Number.isSafeInteger(n));}))throw Error('Unsafe numeric precision. The original input was not imported or rewritten.');try{d=JSON.parse(t);}catch(e){throw Error('Invalid JSON: '+e.message);}const v=validate(d);if(!v.ok)throw Error(v.errors.slice(0,8).join('\n'));return {doc:d,validation:v};}
const overlaps=(a,b)=>a.start<b.start+b.duration&&b.start<a.start+a.duration;
function relevantScale(s,c,target){if(target==='harmony'){if(!s.targets?.length)return true;return s.targets.some(t=>t.type==='chord'&&t.id===c.id);}return false;}
function current(doc,c,target){if(target==='harmony')return (doc.harmony.scales||[]).filter(s=>overlaps(s,c)&&relevantScale(s,c,target));const t=doc.tracks.find(x=>x.id===target);return (t?.directions||[]).filter(d=>d.type==='improvisation'&&overlaps(d,c));}
function allIDs(doc){const ids=new Set();function walk(v){if(!v||typeof v!=='object')return;if(typeof v.id==='string')ids.add(v.id);for(const x of Object.values(v))walk(x);}walk(doc);return ids;}
function freshID(doc,prefix){const ids=allIDs(doc);let i=1;while(ids.has(prefix+i))i++;return prefix+i;}
function referencers(doc,id){const tracks=[];for(const t of doc.tracks||[])for(const d of t.directions||[]){const p=d.parameters?.pitchSource;if(p?.type==='harmonyScales'&&p.scaleIds?.includes(id))tracks.push(t.name||t.id);}return [...new Set(tracks)];}
function apply(doc,chordID,scaleID,target='harmony',adherence='preferred',allowReplace=false){const v=validate(doc);if(!v.ok)throw Error('Fix the document core errors before applying.');if(!v.writable)throw Error('This document version is read-only.');const c=doc.harmony.chords.find(x=>x.id===chordID);if(!c)throw Error('Select a chord first.');const analysis=candidates(doc,c),s=analysis.items.find(x=>x.id===scaleID);if(!s)throw Error('Select one of the available scale cards.');/* Explicit manual Set may write any selected scale pitch collection to Harmony. AUTO-SET retains its separate conservative policy. */
 /* Manual Key & Mode assignments are intentional user choices; compatibility is shown in the UI, not silently overwritten by recommendations. */
 if(!['preferred','strict'].includes(adherence))throw Error('Invalid adherence.');
 if(target!=='harmony'&&!/^4\.1(?:\.|$)/.test(doc.version))throw Error('Track-level improvisation assignment requires OSTF 4.1. This prototype does not migrate versions.');
 const old=current(doc,c,target),exact=old.filter(x=>x.start===c.start&&x.duration===c.duration);
 if(old.length&&!(old.length===1&&exact.length===1)){
  if(!allowReplace)return {needsConfirmation:true,message:'The existing scale covers a different time range. Replace only the selected chord while keeping its surrounding scale?'};
  if(!SA.scope||!SA.scope.plan)throw Error('Range editor is unavailable. The assignment was not changed.');
  const plan=SA.scope.plan(doc,{range:{start:c.start,end:c.start+c.duration,label:'Chord · '+c.label,kind:'chord'},scale:s,target,adherence,mode:'replace'});
  if(!plan.details.complete)throw Error('A referenced or specially targeted scale blocks part of this chord. No data was changed. Edit the linked scale in OSTF Editor or change the referenced direction first.');
  if(plan.details.writes!==1)throw Error('Cannot safely create exactly one scale for this chord. Existing overlapping assignments need review.');
  return {doc:plan.doc,scale:s,replaced:plan.details.replaced,splits:plan.details.splits};
 }
 if(old.length&&!allowReplace)return {needsConfirmation:true,message:'Replace the existing assignment for this exact chord range?'+(target==='harmony'&&referencers(doc,old[0].id).length?' Referenced by: '+referencers(doc,old[0].id).join(', ')+'.':'')+(analysis.context.keyBoundary.length?' Authored key change occurs inside this chord; intentional cross-boundary scale assignment requires confirmation.':''),warnings:s.warnings};
 if(analysis.context.keyBoundary.length&&!allowReplace)return {needsConfirmation:true,message:'This chord contains an authored key change. A single scale assignment may not cover both tonal contexts. Split the chord or confirm the intentional cross-boundary assignment.',warnings:s.warnings};
 const out=clone(doc),label=pitchName(s.tonic)+' '+s.name;
 if(target==='harmony'){
 const entry={id:exact[0]?.id||freshID(out,'scale.adviser.'),start:c.start,duration:c.duration,label,tonic:clone(s.tonic),intervals:[...s.intervals]};
 if(exact[0]){const index=out.harmony.scales.findIndex(x=>x.id===exact[0].id);out.harmony.scales[index]={...out.harmony.scales[index],...entry};}
 else out.harmony.scales.push(entry);
 }else{
 const t=out.tracks.find(x=>x.id===target);if(!t)throw Error('Selected track no longer exists.');if(t.trackType==='drum'||['drum','drums','percussion'].includes(t.type)||['drum','drums','percussion'].includes(t.role))throw Error('Choose a pitched track.');
 const entry={id:exact[0]?.id||freshID(out,'direction.adviser.'),start:c.start,duration:c.duration,type:'improvisation',label,parameters:{pitchSource:{type:'explicitScale',scale:{tonic:clone(s.tonic),intervals:[...s.intervals],label}},adherence}};
 if(!t.directions)t.directions=[];if(exact[0]){const i=t.directions.findIndex(x=>x.id===exact[0].id);t.directions[i]={...t.directions[i],...entry,parameters:{...t.directions[i].parameters,...entry.parameters}};}else t.directions.push(entry);
 }
 return {doc:out,scale:s};}
/**
 * Preview and assign the highest-ranked scale at every eligible chord.
 * Existing manual settings are preserved by default. Every mutation is made
 * in a single cloned OSTF document, so the UI can commit it as one Undo step.
 * Ranking is a heuristic, not a claim of one objectively correct jazz scale.
 */
function autoSetSong(doc,target='harmony',adherence='preferred',mode='fill',choiceById=null,allowExpressive=false){
 const v=validate(doc);
 if(!v.ok)throw Error('Fix the document core errors before auto-setting scales.');
 if(!v.writable)throw Error('This OSTF version is read-only.');
 if(!['fill','replace'].includes(mode))throw Error('Unknown auto-set mode.');
 if(!['preferred','strict'].includes(adherence))throw Error('Unknown adherence.');
 if(target!=='harmony'){
  if(!/^4\.1(?:\.|$)/.test(doc.version))throw Error('Track directions require OSTF 4.1.');
  const track=doc.tracks.find(x=>x.id===target);
  if(!track)throw Error('Selected track no longer exists.');
  if(track.trackType==='drum'||['drum','drums','percussion'].includes(track.type)||['drum','drums','percussion'].includes(track.role))throw Error('Auto-set requires a pitched track.');
 }
 const out=clone(doc),chords=[...doc.harmony.chords].sort((a,b)=>a.start-b.start||a.duration-b.duration),report=[];
 const counts={total:chords.length,apply:0,replace:0,preserved:0,overlap:0,unsupported:0,caution:0,referenced:0,expressive:0};
 // Avoid repeated deep traversal of all IDs when processing a full song.
 const ids=allIDs(out);
 let nextId=1;
 function unique(prefix){let id;do{id=prefix+nextId++;}while(ids.has(id));ids.add(id);return id;}
 for(const chord of chords){
  const a=candidates(doc,chord),s=(choiceById&&a.items.find(x=>x.id===choiceById[chord.id]))||a.items.find(x=>x.id!=='song_mode')||a.items[0],item={chord:chord.label,id:chord.id,start:chord.start,duration:chord.duration,scale:s?pitchName(s.tonic)+' '+s.name:'',action:'',detail:''};
  // Fill means PRESERVE existing assignments, even if a newly proposed scale
  // would now require musical review. Report overlaps separately; never
  // rewrite existing authored directions merely to fix a recommendation.
  if(mode==='fill'){
   const existing=current(out,chord,target);
   if(existing.length){
    const exactExisting=existing.length===1&&existing[0].start===chord.start&&existing[0].duration===chord.duration;
    item.action=exactExisting?'preserved':'overlap';
    item.detail=exactExisting?'Existing exact-range assignment preserved in Fill mode.':'Existing overlapping/partial assignment protected; edit ranges manually.';
    counts[item.action]++;report.push(item);continue;
   }
  }
  if(!s){item.action='unsupported';item.detail=a.context.cur.reason||'No suitable chord-scale candidates.';counts.unsupported++;report.push(item);continue;}
  // No single assignment can safely describe a chord that spans an
  // explicit tonal change: preserve existing fill-mode entries, otherwise skip.
  if(a.context.keyBoundary.length){const old=current(out,chord,target);
   if(old.length&&mode==='fill'){item.action='preserved';item.detail='Existing scale preserved; key change inside chord requires manual review.';counts.preserved++;}
   else{item.action='caution';item.detail='Authored key change occurs inside this chord. Split the chord/scope or review and assign manually.';counts.caution++;}
   report.push(item);continue;
  }
  if(s.id==='song_mode'){
   if(target==='harmony'||adherence!=='preferred'||!allowExpressive){item.action='caution';item.detail='Song-key mode is a pitched-track Performance reference. Use Preferred adherence and explicitly opt in; never write to shared Harmony.';counts.caution++;report.push(item);continue;}
   if(s.missing.length||s.extra.length||s.melOutside.length){item.action='caution';item.detail='Song-key collection conflicts with required chord tones, authored tensions or fixed melody. Borrowed chords require manual review even with opt-in.';counts.caution++;report.push(item);continue;}
  }
  const ambiguousKey=!a.context.key&&a.context.cur.supported&&!a.context.cur.intervals.some(n=>n===3||n===4);
  if(ambiguousKey && !(mode==='fill' && current(out,chord,target).length)){item.action='caution';item.detail='Key and chord quality are unresolved (no 3rd). Preview only; choose a scale manually or supply tonal context.';counts.caution++;report.push(item);continue;}
  // A scale missing an explicitly authored chord tone or concrete melody pitch
  // is not safe enough to assign silently, regardless of its score.
  // Opt-in permits intentionally incomplete *phrase vocabularies* only on an
  // OSTF 4.1 pitched performance track. NEVER override authored color tones,
  // melody conflicts, or ambiguities. Harmony scales remain strict.
  const phraseVocabulary=['major_pent','minor_pent','blues','blues_tonic','song_mode','bebop_major','bebop_dominant'].includes(s.id);
  const expressiveOK=!!allowExpressive&&target!=='harmony'&&adherence==='preferred'&&phraseVocabulary&&!s.extra.length&&!s.melOutside.length;
  if((s.missing.length&&!expressiveOK)||s.extra.length||s.melOutside.length){item.action='caution';item.detail=[...s.warnings].join(' ')||'Check the chord or melody first.';counts.caution++;report.push(item);continue;}
  const old=current(out,chord,target),exact=old.length===1&&old[0].start===chord.start&&old[0].duration===chord.duration;
  if(old.length&&!exact){item.action='overlap';item.detail='Partial/overlapping assignments are not overwritten.';counts.overlap++;report.push(item);continue;}
  if(old.length&&mode==='fill'){item.action='preserved';item.detail='Existing scale preserved.';counts.preserved++;report.push(item);continue;}
  if(old.length&&target==='harmony'&&referencers(out,old[0].id).length){item.action='referenced';item.detail='Existing scale is referenced by a track.';counts.referenced++;report.push(item);continue;}
  // Bebop, pentatonic and blues are often PERFORMANCE vocabularies, not a
  // complete shared harmonic scale. Never write them silently to Harmony.
  if(target==='harmony'&&['major_pent','minor_pent','blues','blues_tonic','song_mode','bebop_major','bebop_dominant'].includes(s.id)){
   item.action='caution';item.detail='Performance phrase vocabulary (not a complete harmony definition). Use an eligible OSTF 4.1 pitched track or set manually after review.';counts.caution++;report.push(item);continue;
  }
  const label=pitchName(s.tonic)+' '+s.name;
  if(target==='harmony'){
   const entry={id:exact?old[0].id:unique('scale.adviser.'),start:chord.start,duration:chord.duration,label,tonic:clone(s.tonic),intervals:[...s.intervals]};
   if(exact){const idx=out.harmony.scales.findIndex(x=>x.id===old[0].id);out.harmony.scales[idx]={...out.harmony.scales[idx],...entry};}
   else out.harmony.scales.push(entry);
  }else{
   const track=out.tracks.find(x=>x.id===target);
   if(!Array.isArray(track.directions))track.directions=[];
   const entry={id:exact?old[0].id:unique('direction.adviser.'),start:chord.start,duration:chord.duration,type:'improvisation',label,parameters:{pitchSource:{type:'explicitScale',scale:{tonic:clone(s.tonic),intervals:[...s.intervals],label}},adherence}};
   if(exact){const idx=track.directions.findIndex(x=>x.id===old[0].id);track.directions[idx]={...track.directions[idx],...entry,parameters:{...track.directions[idx].parameters,...entry.parameters}};}
   else track.directions.push(entry);
  }
  item.action=exact?'replace':'apply';item.detail=expressiveOK&&s.missing.length?'EXPRESSIVE PHRASE · authored chord tones may be omitted; performance-track direction only.':(s.warnings.length?s.warnings.slice(0,2).join(' '):'Highest-ranked eligible candidate.');if(expressiveOK&&s.missing.length)counts.expressive++;
  counts[exact?'replace':'apply']++;
  report.push(item);
 }
 return {doc:out,counts,report,target,mode};
}
function remove(doc,chordID,target){const c=doc.harmony.chords.find(x=>x.id===chordID);if(!c)throw Error('No selected chord.');if(!validate(doc).writable)throw Error('Read-only version.');const old=current(doc,c,target);if(old.length!==1||old[0].start!==c.start||old[0].duration!==c.duration)throw Error('Only one exact-range assignment can be removed here.');if(target==='harmony'&&referencers(doc,old[0].id).length)throw Error('This harmony scale is referenced by a Track Direction; removal is blocked to protect references.');const out=clone(doc);if(target==='harmony')out.harmony.scales=out.harmony.scales.filter(x=>x.id!==old[0].id);else{const t=out.tracks.find(x=>x.id===target);t.directions=t.directions.filter(x=>x.id!==old[0].id);}return out;}
function noteInfo(s,c){return s.intervals.map((i,k)=>{const pc=mod(s.root+i),chord=c.pcs.includes(pc),passing=(s.id==='bebop_dominant'&&i===11)||(s.id==='bebop_major'&&i===8);const caution=!chord&&((c.intervals.includes(4)&&pc===mod(c.root+5))||(c.intervals.includes(7)&&pc===mod(c.root+8))||passing);return {pc,interval:i,name:spellDegree(s.tonic,s.degrees[k]),degree:s.degrees[k],role:chord?'Chord tone':passing?'Passing tone':caution?'Sustain caution':'Color tone',kind:chord?'chord':caution?'caution':'color'};});}
function duration(doc){return Math.max(doc.timeline.duration||0,...doc.harmony.chords.map(c=>c.start+c.duration),...(doc.timeline.sections||[]).map(s=>s.start+s.duration));}
function position(doc,tick){let bar=1,at=0,ms=[...doc.timeline.meterChanges].sort((a,b)=>a.at-b.at),m=ms[0]||{numerator:4,denominator:4};for(let i=1;i<ms.length&&ms[i].at<=tick;i++){let span=doc.ppq*4/m.denominator*m.numerator;bar+=Math.ceil((ms[i].at-at)/span);at=ms[i].at;m=ms[i];}const unit=doc.ppq*4/m.denominator,bs=unit*m.numerator;const bars=Math.floor((tick-at)/bs),beat=1+((tick-at)%bs)/unit;return {bar:bar+bars,beat:Math.round(beat*100)/100,meter:m};}
function voiceLeading(c,n){if(!n)return [];const guides=c.intervals.filter(x=>[3,4,9,10,11].includes(x)).slice(0,3);if(!guides.length)guides.push(0);return guides.map(i=>{const pc=mod(c.root+i),opts=n.pcs.map(x=>{let step=mod(x-pc);if(step>6)step-=12;return {pc:x,step};}).sort((a,b)=>Math.abs(a.step)-Math.abs(b.step));const d=opts[0];return {from:FL[pc],to:FL[d.pc],step:d.step};});}
function demo(minor=false){const ppq=960,bar=3840;const labels=minor?['Bm7b5','E7b9','AmMaj7','Am6','Dm7','G7','Cmaj7','E7alt']:['Dm7','G7','Cmaj7','A7','Dm7','G7alt','Cmaj7','C6'];const chords=labels.map((label,i)=>{const c={id:'chord.'+(i+1),start:i*bar,duration:bar,label},p=parseChord(c);return {...c,root:p.tonic,intervals:p.intervals};});
return {format:'OSTF',version:'4.1.0',ppq,song:{title:minor?'Minor colors — iiø–V–i':'Major colors — ii–V–I',genre:'Jazz',key:{state:'tonal',tonic:{step:minor?'A':'C',alter:0},mode:minor?'minor':'major'}},timeline:{duration:8*bar,sections:[{id:'section.1',name:minor?'Minor cadence':'Major cadence',role:'verse',start:0,duration:4*bar},{id:'section.2',name:'Color variation',role:'chorus',start:4*bar,duration:4*bar}],tempoChanges:[{at:0,bpm:110}],meterChanges:[{at:0,numerator:4,denominator:4}]},harmony:{chords,scales:[]},tracks:[{id:'track.solo',name:'Solo',role:'melody',trackType:'melodic',clips:[]},{id:'track.bass',name:'Bass',role:'bass',trackType:'melodic',clips:[]}],soundSlots:[],lyrics:{documents:[],phrases:[],units:[],alignments:[]},resources:[]};}
const api={version:'0.8.0',mod,clone,PC,pitchName,spelled,spellDegree,parseChord,scales,defs,effectiveKey,labelKey,estimateKey,context,candidates,validate,parse,current,apply,autoSetSong,remove,noteInfo,voiceLeading,demo,duration,position,referencers};if(typeof module!=='undefined'&&module.exports)module.exports=api;root.SA=api;
})(typeof window!=='undefined'?window:globalThis);

/* OSTF Scale Editor v0.4 — chord-to-chord flow analysis and bounded songwide suggestion.
 * Heuristics are transparent musical aids, never assertions of a unique correct scale.
 * This module does not mutate OSTF; only core.apply/core.autoSetSong write assignments.
 */
(function(root){
'use strict';
const SA=root.SA;
const namesSharp=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
const namesFlat=['C','Db','D','Eb','E','F','Gb','G','Ab','A','Bb','B'];
const unique=xs=>[...new Set(xs)];
const mod=SA.mod;
const fmt=pc=>(namesFlat[mod(pc)]);
const sorted=doc=>[...doc.harmony.chords].sort((a,b)=>a.start-b.start||a.duration-b.duration);
const category=s=>!s?'unknown':s.group==='Tension'?'tension':s.group==='Color'?'color':'inside';
const familyColor={tension:'#d1a168',color:'#a596e7',inside:'#62c7b5',unknown:'#6d869e'};
function pcsOf(item){return item?.pcs||[];}
function contiguous(a,b){return !!(a&&b&&a.start+a.duration===b.start);}
function savedInfo(doc,chord,target,analysis){
  const matches=SA.current(doc,chord,target);
  if(!matches.length)return {status:'none',scale:null,label:''};
  const exact=matches.length===1&&matches[0].start===chord.start&&matches[0].duration===chord.duration;
  if(!exact)return {status:'overlap',scale:null,label:'Overlapping scope'};
  const x=matches[0],s=x.type==='improvisation'?x.parameters?.pitchSource?.scale:x;
  if(!s?.tonic||!Array.isArray(s.intervals))return {status:'custom',scale:null,label:x.label||'Custom instruction'};
  const rootPC=SA.PC(s.tonic);
  if(rootPC==null)return {status:'custom',scale:null,label:x.label||'Unknown scale'};
  const pcs=unique(s.intervals.map(i=>mod(rootPC+i))).sort((a,b)=>a-b);
  const match=(analysis?.items||[]).find(z=>z.pcs.length===pcs.length&&z.pcs.every(p=>pcs.includes(p)));
  return {status:'exact',scale:match||{id:'__fixed__'+chord.id,name:s.label||x.label||'Custom scale',tonic:s.tonic,root:rootPC,pcs,intervals:s.intervals,score:64,group:'Color',missing:[],extra:[],melOutside:[],warnings:[],notes:['Authored scale preserved as a fixed reference.'],fixed:true},label:x.label||s.label||'Assigned'};
}
function suitability(s,ch,context,strategy){
  if(!s||!Number.isFinite(s.score))return -1000;
  let score=s.score;
  const unsafe=(s.missing?.length||0)+(s.extra?.length||0)+(s.melOutside?.length||0);
  score-=unsafe*10;
  if(strategy==='smooth'){
    score-=Math.min(5,(s.keyOutside?.length||0))*1.5;
    if(s.group==='Inside')score+=1;
  }else if(strategy==='tension'){
    const dom=context?.cur?.family==='dominant';
    const moving=context?.cadence||context?.sub;
    if(dom&&moving&&s.group==='Tension')score+=7;
    if(dom&&moving&&s.id==='altered')score+=3;
    if(dom&&!moving&&s.group==='Tension')score-=4;
  }
  return score;
}
function keyBreak(doc,a,b){
 if(!a||!b)return false;
 // A written modulation at or between chord boundaries starts a new harmonic
 // phrase. Two keys with the same pitch classes but different tonic/mode still
 // have different musical function (e.g. C major -> A minor).
 if((doc.timeline?.keyChanges||[]).some(x=>Number.isFinite(x.at)&&x.at>a.start&&x.at<=b.start))return true;
 const left=SA.effectiveKey(doc,a.start),right=SA.effectiveKey(doc,b.start);
 if(!left&&!right)return false;
 if(!left||!right)return true;
 return left.root!==right.root||left.mode!==right.mode;
}
function connection(a,b,chA,chB,strategy,resetAtKey=false){
  if(resetAtKey)return {total:0,shared:0,chromatic:0,resolved:0,text:'Authored key/mode boundary · evaluate a new tonal phrase'};
  if(!contiguous(chA,chB)||!pcsOf(a).length||!pcsOf(b).length)return {total:0,shared:0,chromatic:0,resolved:0,text:'Separate phrases'};
  const c1=SA.parseChord(chA),c2=SA.parseChord(chB);
  const shared=a.pcs.filter(pc=>b.pcs.includes(pc)).length;
  const sharedChord=c1.supported&&c2.supported?c1.pcs.filter(pc=>c2.pcs.includes(pc)).length:0;
  const tensionNotes=c1.supported?a.pcs.filter(pc=>!c1.pcs.includes(pc)):[];
  const nextChord=c2.supported?c2.pcs:[];
  const stepNotes=tensionNotes.filter(pc=>nextChord.some(p=>Math.min(mod(pc-p),mod(p-pc))===1));
  const chromatic=stepNotes.length;
  const guideNotes=c1.supported?c1.pcs.filter(pc=>[3,4,10,11].some(n=>mod(c1.root+n)===pc)):[];
  const resolved=guideNotes.filter(pc=>nextChord.some(p=>Math.min(mod(pc-p),mod(p-pc))<=2)).length;
  // Shared notes facilitate continuity; chromatic semitone approaches can be
  // equally valuable in functional jazz harmony. Scale sets alone cannot
  // prescribe an actual melody, so connection weights remain modest.
  const dominant=c1.supported&&c2.supported&&c1.family==='dominant'&&mod(c1.root-c2.root)===7;
  const weight=strategy==='smooth'?2.1:strategy==='tension'?0.75:1.3;
  const sharedTerm=(shared-3)*weight;
  let approach=Math.min(3,chromatic)*(!dominant?0.55:strategy==='tension'?2.7:1.5);
  // Reward the *option* of resolving altered tension, not unrestricted dissonance.
  if(dominant&&strategy==='tension'&&a.group==='Tension')approach+=2;
  const total=Math.max(-9,Math.min(15,sharedTerm+approach+(resolved?Math.min(2,resolved)*.6:0)));
  let text=dominant&&chromatic?'Altered-note resolution options':shared>=6?'Shared-note continuity':chromatic?'Semitone approach options':sharedChord?'Shared harmony tones':'Contrasting scale colors';
  return {total,shared,chromatic,resolved,text,dominant};
}
function plan(doc,target='harmony',strategy='balanced',lockExisting=true,approach='chord',genreOverride='auto',allowExpressive=false){
  const mode=['balanced','smooth','tension'].includes(strategy)?strategy:'balanced';
  const cs=sorted(doc),data=[],prev=[];
  for(let i=0;i<cs.length;i++){
    const ch=cs[i],analysis=SA.candidates(doc,ch),saved=savedInfo(doc,ch,target,analysis);
    // Score every candidate first: limiting the base-scored first eight hid
    // the ninth (Blues) option from style-aware whole-song planning.
    const phraseSet=new Set(['major_pent','minor_pent','blues','blues_tonic','song_mode','bebop_major','bebop_dominant']);
    const scored=analysis.items.map((s,index)=>({s,index,score:suitability(s,ch,analysis.context,mode)+
      (SA.universal?SA.universal.relevance(s,analysis.context,approach).bonus:0)+
      (SA.genre?SA.genre.relevance(s,doc,ch,analysis.context,approach,genreOverride).bonus:0)}));
    const safe=scored.filter(x=>{
      const s=x.s;
      if(s.extra.length||s.melOutside.length)return false;
      if(target==='harmony'&&phraseSet.has(s.id))return false;
      // A song-wide modal reference is only suitable for a pitched Performance
      // track with explicit opt-in. Unlike pentatonic or blues phrase notes,
      // omitted authored chord tones can NEVER be waived here.
      if(s.id==='song_mode')return target!=='harmony'&&approach==='performance'&&allowExpressive&&s.missing.length===0;
      if(!s.missing.length)return true;
      return target!=='harmony'&&approach==='performance'&&allowExpressive&&phraseSet.has(s.id);
    });
    // Include all viable options for this small catalogue, rather than
    // anchoring the choice to chord-only scores before genre scoring.
    let shortlisted=(safe.length?safe:scored.filter(x=>target!=='harmony'||!phraseSet.has(x.s.id))).sort((a,b)=>b.score-a.score||a.index-b.index).slice(0,14);
    if(lockExisting&&saved.status==='exact'&&saved.scale){
      const ref=shortlisted.find(x=>x.s.id===saved.scale.id);
      shortlisted=[ref||{s:saved.scale,index:-1,score:suitability(saved.scale,ch,analysis.context,mode)}];
    }
    if(!shortlisted.length)shortlisted=[{s:{id:'__unknown__'+ch.id,name:'Not analyzed',score:-100,pcs:[],group:'unknown',missing:[],extra:[],melOutside:[],warnings:[]},score:-100,index:-1}];
    const opts=shortlisted.map(x=>x.s),raw=shortlisted.map(x=>x.score);
    const keyReset=i>0&&keyBreak(doc,cs[i-1],ch); // compute once per chord, not once per candidate pair
    const dp=[],paths=[];
    for(let j=0;j<opts.length;j++){
      let best=-Infinity,kbest=-1;
      if(i===0){best=raw[j];}else{
        for(let k=0;k<data[i-1].options.length;k++){
          const edge=connection(data[i-1].options[k],opts[j],cs[i-1],ch,mode,keyReset).total;
          const value=prev[k]+edge+raw[j];
          if(value>best){best=value;kbest=k;}
        }
      }
      dp.push(best);paths.push(kbest);
    }
    data.push({chord:ch,analysis,saved,options:opts,local:raw,paths});
    prev.length=0;prev.push(...dp);
  }
  const chosen={};let idx=prev.length?prev.indexOf(Math.max(...prev)):-1;
  for(let i=data.length-1;i>=0;i--){const d=data[i];chosen[d.chord.id]=d.options[idx]||d.options[0];idx=d.paths[idx]??-1;}
  const byId={};for(const [id,s] of Object.entries(chosen))byId[id]=s.id;
  const counts={total:cs.length,assigned:0,unassigned:0,review:0,unsupported:0};
  for(const d of data){if(d.saved.status==='exact')counts.assigned++;else counts.unassigned++;if(d.saved.status==='overlap'||d.saved.status==='custom'||!d.analysis.items.length)counts.review++;if(!d.analysis.items.length)counts.unsupported++;}
  return {chords:cs,data,chosen,byId,counts,target,strategy:mode,lockExisting,approach,genreOverride,allowExpressive};
}
function ranked(doc,chord,target,strategy,flow,approach='chord',genreOverride='auto'){
  const analysis=SA.candidates(doc,chord),idx=flow.chords.findIndex(c=>c.id===chord.id);
  if(idx<0)return analysis;
  const prevCh=flow.chords[idx-1],nextCh=flow.chords[idx+1];
  const prevChoice=prevCh?flow.chosen[prevCh.id]:null,nextChoice=nextCh?flow.chosen[nextCh.id]:null;
  const breakLeft=!!prevChoice&&keyBreak(doc,prevCh,chord),breakRight=!!nextChoice&&keyBreak(doc,chord,nextCh);
  const sortedItems=analysis.items.map((s,oldIndex)=>{
    const left=prevChoice?connection(prevChoice,s,prevCh,chord,strategy,breakLeft):null;
    const right=nextChoice?connection(s,nextChoice,chord,nextCh,strategy,breakRight):null;
    const base=suitability(s,chord,analysis.context,strategy);
    const relevance=SA.universal?SA.universal.relevance(s,analysis.context,approach):{bonus:0,reason:'Chord/key fit'};
    const genre=SA.genre?SA.genre.relevance(s,doc,chord,analysis.context,approach,genreOverride):{bonus:0,reason:''};
    const value=base+(left?.total||0)+(right?.total||0)+relevance.bonus+genre.bonus;
    return {...s,flowScore:Math.round(value*10)/10,oldIndex,flowLeft:left,flowRight:right,lensBonus:relevance.bonus,lensReason:relevance.reason,genreBonus:genre.bonus,genreReason:genre.reason,genreStatus:genre.style?.id||'general'};
  }).sort((a,b)=>b.flowScore-a.flowScore||a.oldIndex-b.oldIndex);
  sortedItems.forEach((s,i)=>{s.position=i+1;s.rankChange=s.oldIndex-i;s.flowReason=[s.lensReason,s.genreReason,s.flowLeft?.text,s.flowRight?.text].filter(Boolean).join(' · ')||'Chord/key fit';});
  return {...analysis,items:sortedItems};
}
function spellScalePc(scale,pc){
 if(!scale?.pcs?.length||!scale.degrees?.length)return fmt(pc);
 const idx=scale.pcs.indexOf(mod(pc));
 return idx>=0?SA.spellDegree(scale.tonic,scale.degrees[idx]):fmt(pc);
}
function differences(a,b){
  if(!a||!b)return {added:[],removed:[],common:[]};
  return {added:a.pcs.filter(x=>!b.pcs.includes(x)),removed:b.pcs.filter(x=>!a.pcs.includes(x)),common:a.pcs.filter(x=>b.pcs.includes(x))};
}
function resolveNote(curPC,nextPC){let delta=mod(nextPC-curPC);if(delta>6)delta-=12;return {from:fmt(curPC),to:fmt(nextPC),delta};}
function transitions(ch,s,nextChord,nextScale){
  const c=SA.parseChord(ch),n=nextChord?SA.parseChord(nextChord):null;
  if(!c.supported||!n?.supported||!s||!nextScale)return [];
  const guides=c.pcs.filter(p=>[3,4,10,11].some(d=>mod(c.root+d)===p));
  const candidates=unique([...guides,...s.pcs.filter(p=>!c.pcs.includes(p)&&n.pcs.some(z=>Math.min(mod(p-z),mod(z-p))===1))]);
  const rows=[];
  for(const p of candidates){
    const choices=n.pcs.map(z=>resolveNote(p,z)).sort((a,b)=>Math.abs(a.delta)-Math.abs(b.delta));
    const first=choices[0];if(first&&Math.abs(first.delta)<=2){rows.push({...first,from:spellScalePc(s,p),to:spellScalePc(nextScale,mod(p+first.delta)),kind:c.pcs.includes(p)?'guide':'approach',inNext:nextScale.pcs.includes(mod(p+first.delta))});}
  }
  return rows.sort((a,b)=>a.kind===b.kind?Math.abs(a.delta)-Math.abs(b.delta):a.kind==='guide'?-1:1).slice(0,8);
}
function harmonicRole(context){
  if(!context||!context.cur?.supported)return {label:'Unresolved',detail:'Chord could not be analyzed.',level:'uncertain'};
  const c=context.cur,n=context.next,k=context.key;
  const third=c.intervals.some(v=>v===3||v===4);
  if(!third){return {label:'Open harmony / no third',detail:k?'Major/minor quality is unresolved; existing key offers context but does not force a scale.':'No key and no chord third. Major/minor mode cannot be determined; all scale proposals are provisional.',level:'uncertain'};}
  if(context.blues){return {label:'Dominant-blues I7 / IV7 / V7 · hypothesis',detail:'Recurring dominant pattern. Mixolydian is a harmony reference; the tonic minor-blues vocabulary may cross the changes as expressive notes. Check thirds, groove and melody.',level:'context'};}
  if(context.cadence){
    let degree='';
    if(k&&n&&k.mode==='major'){
      const d=mod(n.root-k.root),names={0:'I',2:'ii',4:'iii',5:'IV',7:'V',9:'vi',11:'vii°'};
      degree=names[d]||'';
    }
    const role=degree?(degree==='I'?'V7 → I':'V7/'+degree):'V7 → target';
    return {label:role,detail:'Dominant root resolves by a fifth to '+context.nextEvent.label+'. Evaluate altered tensions against melody and accompaniment.',level:'resolved'};
  }
  if(context.sub){return {label:'Possible tritone substitution',detail:'Semitone-down dominant movement is compatible with tritone substitution, but does not prove one.',level:'context'};}
  if(c.family==='dominant')return {label:'Dominant / no immediate resolution',detail:'Inspect the following harmony before choosing tension-heavy dominant vocabularies.',level:'context'};
  if(!k)return {label:'Local chord fit only',detail:'OSTF contains no effective key at this position. Local chord-scale candidates do not imply a global tonal center.',level:'uncertain'};
  return {label:'Key-aware chord context',detail:'Effective key: '+SA.pitchName(k.tonic)+' '+k.mode+'. Compare melodic emphasis and neighboring harmony.',level:'context'};
}
function noteMatrix(doc,cs,currentIndex,choice,target,flow){
 const rows=[];
 for(let k=currentIndex-1;k<=currentIndex+1;k++){
  const ch=cs[k];if(!ch)continue;
  const parse=SA.parseChord(ch),s=k===currentIndex?choice:flow.chosen[ch.id],assigned=SA.current(doc,ch,target).length>0;
  rows.push({id:ch.id,label:ch.label,role:k===currentIndex?'SELECTED':k<currentIndex?'PREVIOUS':'NEXT',scale:s?.pcs||[],chord:parse?.pcs||[],assigned});
 }
 return rows;
}
function review(doc,target,flow){
  const rows=[];for(const d of flow.data){
    if(d.analysis.context.keyBoundary?.length){rows.push({id:d.chord.id,chord:d.chord.label,level:'conflict',reason:'Authored key change inside chord; split range or manually review.'});continue;}
    if(!d.analysis.context.key && d.analysis.context.cur.supported&&!d.analysis.context.cur.intervals.some(n=>n===3||n===4)){rows.push({id:d.chord.id,chord:d.chord.label,level:'conflict',reason:'Mode unresolved: no key and no chord third. Manual decision required.'});continue;}
    const a=d.analysis,cs=a.items,top=flow.chosen[d.chord.id];
    if(d.saved.status==='overlap'){rows.push({id:d.chord.id,chord:d.chord.label,level:'conflict',reason:'Overlapping scale ranges — editing protected'});continue;}
    if(!cs.length){rows.push({id:d.chord.id,chord:d.chord.label,level:'conflict',reason:a.context.cur.reason||'Unsupported chord'});continue;}
    if(d.saved.status==='exact')continue;
    const s=cs.find(x=>x.id===top.id)||cs[0];
    if(s.missing.length||s.extra.length||s.melOutside.length){rows.push({id:d.chord.id,chord:d.chord.label,level:'conflict',reason:'Compare chord tones / fixed melody before applying'});continue;}
    if(cs.length>1&&Math.abs(cs[0].score-cs[1].score)<7){rows.push({id:d.chord.id,chord:d.chord.label,level:'alternative',reason:'Several musically plausible candidates'});}
  }
  return rows;
}
SA.flow={plan,ranked,connection,keyBreak,savedInfo,differences,transitions,review,harmonicRole,noteMatrix,category,familyColor,fmt,spellScalePc,sorted};
})(typeof globalThis!=='undefined'?globalThis:this);

/* OSTF Scale Editor v0.29: contextual, style-aware A/B scale auditions.
 * Each style has a deterministic rhythm template; A and B share the SAME
 * template, chord backing, bass and context. Only scale color notes change.
 * Illustrative rhythm only; not a claim of authentic genre performance.
 * NEVER edits original OSTF notes or other song data.
 */
(function(root){
'use strict';
const SA=root.SA,mod=SA.mod;

// Fixed illustrative timing, not an arrangement generator. Every entry has
// eight [beat offset, beat duration] melodic slots inside a four-beat chord.
const profiles={
 general:{label:'Neutral / straight',slots:[[.06,.4],[.57,.32],[1.07,.28],[1.57,.36],[2.07,.4],[2.57,.28],[3.07,.31],[3.58,.18]]},
 jazz:{label:'Jazz / swing-like syncopation',slots:[[.07,.43],[.69,.32],[1.04,.28],[1.47,.36],[2.04,.42],[2.76,.24],[3.07,.34],[3.60,.18]]},
 pop:{label:'Pop / straight eighths',slots:[[.04,.42],[.51,.32],[1.03,.28],[1.52,.35],[2.03,.4],[2.51,.27],[3.04,.31],[3.52,.18]]},
 rnb:{label:'R&B / displaced accents',slots:[[.08,.50],[.74,.27],[1.12,.23],[1.72,.37],[2.18,.42],[2.78,.22],[3.14,.29],[3.64,.17]]},
 rock:{label:'Rock / driving eighths',slots:[[.05,.37],[.48,.30],[1.05,.27],[1.48,.40],[2.04,.32],[2.61,.23],[3.03,.29],[3.50,.18]]},
 metal:{label:'Metal / subdivided riff',slots:[[.05,.25],[.33,.23],[.78,.28],[1.27,.35],[1.77,.30],[2.26,.25],[2.77,.32],[3.27,.20]]},
 blues:{label:'Blues / shuffle-like accents',slots:[[.04,.48],[.68,.29],[1.35,.27],[1.79,.38],[2.35,.42],[2.84,.23],[3.35,.25],[3.70,.17]]},
 hiphop:{label:'Hip-Hop / half-time space',slots:[[.11,.46],[.83,.24],[1.16,.24],[1.84,.32],[2.11,.46],[2.69,.23],[3.16,.27],[3.69,.16]]},
 house:{label:'House / offbeat groove',slots:[[.06,.35],[.74,.23],[1.26,.27],[1.76,.32],[2.27,.33],[2.76,.25],[3.26,.28],[3.74,.16]]},
 techno:{label:'Techno / repeating pulse',slots:[[.04,.32],[.55,.28],[1.05,.30],[1.55,.32],[2.05,.30],[2.55,.28],[3.05,.28],[3.55,.18]]},
 trance:{label:'Trance / continuous arpeggiation',slots:[[.04,.28],[.51,.29],[1.04,.28],[1.55,.32],[2.05,.30],[2.56,.27],[3.06,.29],[3.56,.22]]},
 ambient:{label:'Ambient / sustained color',slots:[[.04,.58],[.76,.36],[1.38,.38],[1.91,.34],[2.42,.35],[2.90,.23],[3.23,.24],[3.66,.16]]},
 reggae:{label:'Reggae / offbeat emphasis',slots:[[.53,.29],[.96,.25],[1.52,.28],[1.96,.28],[2.52,.30],[2.96,.24],[3.50,.25],[3.78,.14]]},
 edm:{label:'Electronic / syncopated pulse',slots:[[.05,.36],[.52,.28],[1.17,.27],[1.54,.33],[2.03,.30],[2.52,.24],[3.17,.28],[3.52,.19]]}
};

function nearest(pc,anchor=72,low=60,high=84){
 const choices=[];
 for(let n=low;n<=high;n++)if(mod(n)===mod(pc))choices.push(n);
 choices.sort((a,b)=>Math.abs(a-anchor)-Math.abs(b-anchor)||a-b);
 return choices[0]??anchor;
}
function tempoAt(doc,at){let bpm=110;for(const x of [...(doc?.timeline?.tempoChanges||[])].sort((a,b)=>a.at-b.at)){if(x.at<=at&&Number.isFinite(x.bpm))bpm=x.bpm;else if(x.at>at)break;}return Math.max(65,Math.min(180,bpm));}
function contiguous(a,b){return !!(a&&b&&a.start+a.duration===b.start);}
function interval(c,desired,alternative=[7,0]){
 return desired.find(i=>c.intervals.includes(i))??alternative.find(i=>c.intervals.includes(i))??0;
}
function differenceTone(scale,other,chord,index=0){
 const c=SA.parseChord(chord);
 if(!scale?.pcs?.length||!c.supported)return null;
 // In an A/B comparison, favor tones unique to the chosen collection. Never
 // change a guide tone just to manufacture an audible difference.
 const distinct=scale.pcs.filter(pc=>!other?.pcs?.includes(pc)&&!c.pcs.includes(pc));
 const colors=scale.pcs.filter(pc=>!c.pcs.includes(pc));
 const pool=distinct.length?distinct:colors.length?colors:scale.pcs;
 return pool[index%pool.length];
}
function buildPhrase(doc,chord,following,scale,reference,preceding=null,style='general'){
 const profile=profiles[style]||profiles.general;
 const c=SA.parseChord(chord),p=contiguous(preceding,chord)?SA.parseChord(preceding):null,n=contiguous(chord,following)?SA.parseChord(following):null;
 if(!c.supported||!scale)return null;
 const hasPrevious=!!p?.supported,hasNext=!!n?.supported;
 const bpm=tempoAt(doc,chord.start),beatMs=60000/bpm;
 const selectedStart=hasPrevious?2:0,nextStart=selectedStart+4;
 const durationBeats=nextStart+(hasNext?2.05:.18);
 const regions=[];
 if(hasPrevious)regions.push({role:'previous',start:0,end:selectedStart,label:preceding.label});
 regions.push({role:'selected',start:selectedStart,end:nextStart,label:chord.label});
 if(hasNext)regions.push({role:'next',start:nextStart,end:durationBeats,label:following.label});
 const third=interval(c,[3,4],[5,7,2]),seventh=interval(c,[10,11,9],[7,5]);
 const root=nearest(c.root,67,60,82),thirdNote=nearest(c.root+third,72,61,84),sevenNote=nearest(c.root+seventh,74,62,84);
 const fifthNote=nearest(c.root+interval(c,[7,6,8],[0]),75,62,85);
 const colorA=differenceTone(scale,reference,chord,0),colorB=differenceTone(scale,reference,chord,1);
 const colorNoteA=nearest(colorA??c.root,76,64,85),colorNoteB=nearest(colorB??c.root,73,62,85);
 const line=[];
 function point(beat,note,role,msBeats=.25){line.push({beat:+beat.toFixed(3),note,role,duration:msBeats});}
 // A short ii→V pickup line. The swing-like offbeat placement is shared.
 if(hasPrevious){
  const p3=interval(p,[3,4],[7,0]);
  point(.14,nearest(p.root,68,60,83),'lead-in',.43);
  point(.80,nearest(p.root+p3,71,60,84),'lead-in',.39);
  point(1.40,nearest(p.root+interval(p,[10,11,7],[0]),72,60,84),'lead-in',.33);
  point(1.76,nearest(c.root+(c.root===p.root?7:0),67,60,84),'approach',.19);
 }
 // Rhythm is style-aware but shared by A and B; harmony and bass stay fixed.
 // Color tones are intentionally the ONLY variable melodic pitch slots.
 const nextThird=hasNext?interval(n,[3,4],[7,0]):0;
 const nextTarget=hasNext?nearest(n.root+nextThird,73,63,85):root;
 const notes=[root,thirdNote,colorNoteA,sevenNote,fifthNote,colorNoteB,sevenNote,
              hasNext?nextTarget+(nextTarget<84?-1:1):thirdNote];
 const roles=['root','guide',!reference?.pcs?.includes(mod(colorNoteA))?'difference':'color',
              'guide','chord',!reference?.pcs?.includes(mod(colorNoteB))?'difference':'color',
              'guide','approach'];
 profile.slots.forEach(([beat,duration],i)=>point(selectedStart+beat,notes[i],roles[i],duration));
 if(hasNext){
  point(nextStart+.06,nextTarget,'resolution',.42);
  point(nextStart+.76,nearest(n.root,71,62,84),'resolution',.32);
  point(nextStart+1.37,nearest(n.root+interval(n,[10,11,7],[0]),73,62,84),'resolution',.41);
 }
 const events=[];
 function add(beat,note,duration,velocity,role){
  if(!Number.isInteger(note)||note<0||note>127)return;
  events.push({at:Math.round(beat*beatMs),beat:+beat.toFixed(3),note,ms:Math.max(40,Math.round(duration*beatMs)),velocity,role});
 }
 function comp(ch,at,total,kind){
  // Unchanged bass/shell comping: the harmony is *never* optimized to flatter B.
  const third=interval(ch,[3,4],[5,7,2]),seventh=interval(ch,[10,11,9],[7,0]);
  const bass=nearest(ch.root,43,35,51);
  const thirdN=nearest(ch.root+third,56,50,65),seventhN=nearest(ch.root+seventh,59,51,66);
  add(at+.03,bass,Math.min(.68,total-.08),66,kind+'-bass');
  if(total>1.3)add(at+Math.min(total-.65,1.48),nearest(ch.root+interval(ch,[7,6,8],[0]),43,35,51),.35,56,kind+'-bass');
  if(total>2.4)add(at+Math.min(total-.45,3.00),bass,.38,58,kind+'-bass');
  for(const beat of [at+.10,...(total>=3.8?[at+2.53]:[])]){
   add(beat,thirdN,.60,43,kind+'-pad');
   if(seventhN!==thirdN)add(beat,seventhN,.60,42,kind+'-pad');
  }
 }
 if(hasPrevious)comp(p,0,selectedStart,'previous');
 comp(c,selectedStart,4,'selected');
 if(hasNext)comp(n,nextStart,2,'next');
 for(const item of line){const velocity=item.role==='difference'?94:item.role==='color'?86:item.role==='guide'?87:item.role==='resolution'?89:78;add(item.beat,item.note,item.duration,velocity,item.role);}
 events.sort((a,b)=>a.at-b.at||((a.role.includes('-pad')||a.role.includes('-bass'))?-1:1));
 return {bpm,beatMs,events,line,regions,timelineBeats:durationBeats,duration:Math.round(durationBeats*beatMs),hasPrevious,hasNext,
  scale:SA.pitchName(scale.tonic)+' '+scale.name,chord:chord.label,previous:hasPrevious?preceding.label:null,next:hasNext?following.label:'End',
  changed:!!reference&&reference.pcs.some(pc=>!scale.pcs.includes(pc)),style:profile.label,styleId:profiles[style]?style:'general',auditionOnly:true};
}
function pair(doc,chord,following,ref,chosen,preceding=null,style='general'){
 const a=buildPhrase(doc,chord,following,ref,chosen,preceding,style),b=buildPhrase(doc,chord,following,chosen,ref,preceding,style);
 const diff=SA.flow.differences(chosen,ref);
 return {a,b,diff,different:!!(diff.added.length||diff.removed.length)};
}
SA.audition={nearest,tempoAt,differenceTone,contiguous,phrase:buildPhrase,pair,profiles};
if(typeof module!=='undefined'&&module.exports)module.exports=SA.audition;
})(typeof window!=='undefined'?window:globalThis);

/* OSTF Scale Editor v0.17: musical analysis lenses.
 * A lens ranks already-analyzed candidate scales; it NEVER assigns notes or
 * changes OSTF song/key/harmony. All modes preserve score safety checks.
 */
(function(root){
'use strict';
const SA=root.SA;
const perf=new Set(['major_pent','minor_pent','blues','blues_tonic','song_mode','bebop_dominant','bebop_major']);
const chromatic=new Set(['altered','phrygian_dominant','half_whole','whole_half','whole_tone','lydian_dominant','lydian_augmented','mixolydian_b6','dorian_b2']);
const modeList=[
 {id:'chord',label:'Chord Scale',short:'Chord tones · resolution',why:'Functional harmony, chord-scale color and the next chord'},
 {id:'key',label:'Key & Mode',short:'Tonal center · continuity',why:'Favor a shared key or modal collection across chords'},
 {id:'performance',label:'Performance',short:'Phrases · expressive color',why:'Highlight pentatonic, blues and bebop phrase vocabularies'}
];
function roles(s){
 if(!s)return ['Unknown'];
 const a=[];
 if(!perf.has(s.id))a.push('Harmony');
 if(perf.has(s.id))a.push('Phrase');
 if(chromatic.has(s.id)||['blues','blues_tonic'].includes(s.id))a.push('Color');
 if(s.id.startsWith('bebop'))a.push('Passing tone');
 if(s.id.endsWith('_pent'))a.push('Reduced notes');
 if(s.id==='lydian'||s.id==='phrygian')a.push('Modal color');
 if(s.id==='song_mode')a.push('Key & Mode');
 return a;
}
function tonalContext(context){
 const k=context?.key||context?.estimatedKey;
 if(!k?.pcs?.length)return {status:'unresolved',tonic:null,pcs:[],label:'Key / mode unresolved',authoritative:false,confidence:'unknown'};
 const authored=!!context.key;
 return {status:authored?'authored':'hypothesis',tonic:k.tonic,pcs:k.pcs,label:SA.pitchName(k.tonic)+' '+k.mode,authoritative:authored,confidence:authored?'authored':k.confidence||'low'};
}
function relevance(scale,context,lens){
 if(!scale?.pcs?.length)return {bonus:0,reason:'Unresolved scale',keyOverlap:0,uncertain:true};
 const key=tonalContext(context),inside=key.pcs.length?scale.pcs.filter(x=>key.pcs.includes(x)).length:0;
 const out=scale.pcs.length-inside;
 const unresolved=!key.pcs.length;
 const coreMiss=(scale.missing?.length||0)+(scale.extra?.length||0);
 const melMiss=scale.melOutside?.length||0;
 let bonus=0,reason='Chord tones and harmonic resolution';
 if(lens==='key'){
   if(!unresolved){
     // Do not over-penalize chromatic functional dominants in minor cadences.
     const minorCadence=context?.cadence&&context?.minorResolve;
     bonus=(inside/scale.pcs.length)*16-out*(minorCadence?1.5:2.2);
     if(key.status==='hypothesis')bonus*=key.confidence==='moderate'?.55:.25;
     if(scale.id==='song_mode'&&key.authoritative)bonus+=12;
     reason=scale.id==='song_mode'?'Same authored tonic and mode across chord roots':inside+' / '+scale.pcs.length+' tones in '+(key.status==='hypothesis'?'hypothesized':'authored')+' tonal collection';
   }else{reason='No authored or usable key; comparison remains provisional';}
 }else if(lens==='performance'){
   if(perf.has(scale.id))bonus+=16;
   if(scale.id==='song_mode'&&key.authoritative)bonus+=9;
   if(scale.id==='major_pent'||scale.id==='minor_pent')bonus+=3;
   if(scale.id.startsWith('bebop'))bonus+=2;
   if(scale.id==='blues'||scale.id==='blues_tonic')bonus+=1;
   if(chromatic.has(scale.id)&&context?.cadence)bonus+=2;
   reason=scale.id==='song_mode'?'Authored song-key mode throughout chord changes, for melodic continuity':perf.has(scale.id)?'Expressive phrase vocabulary; not a full harmonic definition':'Harmonic reference for an improvised phrase';
 }else if(lens==='chord'){
   // Existing sophisticated functional and voice-leading scores are authoritative
   // for this lens; the role badge is explanatory only.
   reason=context?.cadence?'Resolution-aware chord-scale choice':'Chord tones / local harmonic function';
 }
 // The detailed core warnings are always preserved. A visually promoted
 // phrase vocabulary must never be claimed to fully cover the chord.
 if((coreMiss||melMiss)&&lens==='performance')reason+=' · inspect omitted tones / melody';
 return {bonus,reason,keyOverlap:inside,keyOut:out,tonal:key,uncertain:unresolved};
}
function insight(doc,chord,s,lens){
 const cx=SA.context(doc,chord),tc=tonalContext(cx),parsed=cx.cur;
 const pcChord=parsed.supported?parsed.pcs:[];
 const role=roles(s),rel=s?relevance(s,cx,lens):null;
 const candidates=SA.candidates(doc,chord).items;
 const fit=!!s&&!s.missing?.length&&!s.extra?.length&&!s.melOutside?.length;
 const stable=!!(tc.authoritative&&parsed.supported&&parsed.intervals.some(x=>x===3||x===4));
 const status=!tc.pcs.length?'No tonal center confirmed':tc.status==='authored'?'Authored key / mode':'Tonal hypothesis · not saved';
 const caution=!parsed.supported?'Chord not analyzed':cx.keyBoundary?.length?'Authored key changes within this chord; split or review manually':!parsed.intervals.some(x=>x===3||x===4)&&!tc.authoritative?'No chord third or confirmed key':s?.melOutside?.length?'Authored melody contains outside pitch classes':s?.missing?.length||s?.extra?.length?'Some chord tones or authored tensions are absent':!stable?'Interpretation is provisional':'Multiple choices may be valid';
 const section=doc.timeline?.sections?.find(x=>x.start<=chord.start&&chord.start<x.start+x.duration);
 return {lens,role,tonal:tc,rel,pcChord,fit,stable,status,caution,section:section?.name||'Song',selected:s?.name||'None',candidateCount:candidates.length};
}
function summary(lens){return modeList.find(x=>x.id===lens)||modeList[0];}
function demoStyle(kind){
 const d=SA.demo(false),bar=4*d.ppq;
 const patterns={
  pop:{name:'Pop — Key Center',key:{state:'tonal',tonic:{step:'C',alter:0},mode:'major'},chords:['C','G','Am','F','C','G','Am','F']},
  modal:{name:'Modal Groove — D Dorian',key:{state:'tonal',tonic:{step:'D',alter:0},mode:'dorian'},chords:['Dm7','G7','Dm7','Am7','Dm7','G7','Dm7','Am7']},
  power:{name:'Rock — Power Chord Ambiguity',key:{state:'unspecified'},chords:['E5','G5','D5','E5','A5','G5','D5','E5']},
  blues:{name:'Blues — Twelve-Bar I7 IV7 V7',key:{state:'tonal',tonic:{step:'C',alter:0},mode:'major'},chords:['C7','F7','C7','C7','F7','F7','C7','C7','G7','F7','C7','G7']},
  trance:{name:'Trance — A Minor Loop',key:{state:'tonal',tonic:{step:'A',alter:0},mode:'minor'},chords:['Am','F','C','G','Am','F','C','G']}
 };
 const data=patterns[kind];if(!data)return null;
 d.song.title=data.name;d.song.key=data.key;d.song.genre=kind==='pop'?'Pop':kind==='power'?'Rock':kind==='blues'?'Blues':kind==='trance'?'Trance':'Electronic';
 d.timeline.duration=data.chords.length*bar;d.timeline.sections=[{id:'section.a',name:'A',role:'verse',start:0,duration:4*bar},{id:'section.b',name:'B',role:'chorus',start:4*bar,duration:(data.chords.length-4)*bar}];
 d.harmony.chords=data.chords.map((label,i)=>{const base={id:'chord.'+(i+1),label,start:i*bar,duration:bar},c=SA.parseChord(base);return {...base,root:c.tonic,intervals:c.intervals};});
 return d;
}
SA.universal={modeList,roles,tonalContext,relevance,insight,summary,demoStyle};
})(typeof globalThis!=='undefined'?globalThis:this);

/* v0.28 — musical style lens. This layer is read-only and never edits OSTF.
 * Distinct subgenre engines are advisory weightings, not genre stereotypes.
 * Authored chord notes and concrete melody conflicts are evaluated upstream.
 */
(function(root){
'use strict';
const SA=root.SA;
const styles=[
 {id:'auto',label:'Auto (OSTF)'},{id:'general',label:'General / neutral'},
 {id:'jazz',label:'Jazz'},{id:'pop',label:'Pop / J-Pop'},
 {id:'rnb',label:'R&B / Soul / Funk'},{id:'rock',label:'Rock / Punk'},
 {id:'metal',label:'Metal'},{id:'blues',label:'Blues'},
 {id:'hiphop',label:'Hip-Hop / Trap'},{id:'house',label:'House'},
 {id:'techno',label:'Techno'},{id:'trance',label:'Trance'},
 {id:'ambient',label:'Ambient / Downtempo'},
 {id:'reggae',label:'Reggae / Ska'},
 {id:'edm',label:'EDM / Electronic (general)'}
];
const STYLE_IDS=new Set(styles.map(x=>x.id));
function stringifyValue(x){
 if(typeof x==='string')return x;
 if(Array.isArray(x))return x.map(stringifyValue).filter(Boolean).join(' / ');
 if(x&&typeof x==='object')return [x.name,x.label,x.id,x.value,x.primary].filter(v=>typeof v==='string').join(' / ');
 return '';
}
function metadata(doc){
 // Specific subgenre tags take priority over broad song.genre / Electronic tags.
 return [doc?.song?.subgenre,doc?.song?.subgenres,doc?.song?.style,
         doc?.song?.genre,doc?.song?.genres,doc?.metadata?.subgenre,
         doc?.metadata?.genre,doc?.metadata?.genres].map(stringifyValue).filter(Boolean).join(' / ');
}
function classify(str){
 const t=String(str||'').toLowerCase().replace(/[‐‑‒–—−]/g,'-');
 // Specific labels precede broad umbrella terms. Japanese genre metadata too.
 if(/\b(blues|delta blues|chicago blues)\b|ブルース/.test(t))return 'blues';
 if(/\b(jazz|bebop|swing|hard bop|bossa nova|modal jazz)\b|ジャズ/.test(t))return 'jazz';
 if(/\b(black metal|death metal|heavy metal|metalcore|metal|djent)\b|メタル/.test(t))return 'metal';
 if(/\b(hip-hop|hip hop|hiphop|trap|boom bap|drill)\b|ヒップホップ|トラップ/.test(t))return 'hiphop';
 if(/\b(r&b|rnb|neo soul|neo-soul|soul|funk|gospel)\b|ソウル|ファンク/.test(t))return 'rnb';
 if(/\b(trance|psytrance|uplifting trance|progressive trance)\b|トランス/.test(t))return 'trance';
 if(/\b(techno|minimal techno|hard techno|dub techno)\b|テクノ/.test(t))return 'techno';
 if(/\b(house|deep house|progressive house|tech house|electro house|afro house)\b|ハウス/.test(t))return 'house';
 if(/\b(ambient|downtempo|chillout|chill-out|drone)\b|アンビエント|ダウンテンポ/.test(t))return 'ambient';
 if(/\b(reggae|ska|dub reggae|dancehall)\b|レゲエ/.test(t))return 'reggae';
 if(/\b(rock|punk|grunge|post-rock|hardcore|shoegaze|indie rock|alt rock)\b|ロック|パンク/.test(t))return 'rock';
 if(/\b(pop|j-pop|jpop|k-pop|city pop|synthpop|dance pop|electropop|idol)\b|ポップ|歌謡曲/.test(t))return 'pop';
 if(/\b(edm|electronic|electro|dubstep|drum and bass|dnb|breakbeat|garage|hardstyle|bass music)\b|エレクトロ|電子音楽/.test(t))return 'edm';
 return 'general';
}
function resolve(doc,override='auto'){
 if(!STYLE_IDS.has(override))override='auto';
 const raw=metadata(doc),id=override==='auto'?classify(raw):override;
 const found=styles.find(x=>x.id===id);
 return {id,label:found?.label||'General / neutral',source:override==='auto'?(raw?'OSTF metadata':'No genre metadata'):'Manual analysis override',raw,override,authored:override==='auto'&&!!raw,neutral:id==='general'};
}
const exotic=new Set(['altered','half_whole','whole_half','whole_tone','phrygian_dominant','lydian_dominant','dorian_b2','mixolydian_b6','lydian_augmented']);
const bebop=new Set(['bebop_major','bebop_dominant']);
const pent=new Set(['major_pent','minor_pent']);
const modes=new Set(['ionian','aeolian','dorian','phrygian','lydian','mixolydian']);
function tonal(s,cx,weight){
 const k=cx?.key||(cx?.estimatedKey?.confidence==='moderate'?cx.estimatedKey:null);
 if(!k?.pcs?.length)return {bonus:0,available:false,authored:false};
 const inside=s.pcs.filter(x=>k.pcs.includes(x)).length;
 const outside=s.pcs.length-inside;
 // A locally guessed key has intentionally less influence than an authored key.
 const bonus=((inside/Math.max(1,s.pcs.length))*weight-outside*weight*.44)*(cx.key?1:.28);
 return {bonus,inside,outside,available:true,authored:!!cx.key};
}
function result(bonus,reason){return {bonus,reason};}
function jazz(s,cx,lens){
 let b=0,c=cx.cur;
 if(c.family==='dominant'){
  if(s.id==='mixolydian')b+=cx.cadence?3:7;
  if(s.id==='altered'&&cx.cadence)b+=5;
  if(exotic.has(s.id)&&!cx.cadence&&!cx.sub)b-=4;
 }
 if(c.family==='minor'&&s.id==='dorian')b+=5;
 if(c.family==='major7'&&s.id==='lydian')b+=3;
 if(lens==='performance'&&bebop.has(s.id))b+=8;
 if(s.id==='blues'&&!cx.blues)b-=4;
 return result(b,'Jazz: harmonic function, guide-tone motion and altered resolution');
}
function pop(s,cx,lens){
 const t=tonal(s,cx,15);let b=t.bonus;
 if(['ionian','aeolian','dorian','mixolydian'].includes(s.id))b+=4;
 if(lens==='performance'&&pent.has(s.id))b+=16;
 if(lens==='performance'&&s.id==='blues')b+=1;
 if(bebop.has(s.id))b-=14;
 if(exotic.has(s.id))b-=9;
 if(cx.cadence&&cx.minorResolve&&s.id==='phrygian_dominant')b+=4;
 return result(b,t.available?'Pop: authored tonal center and singable continuity take priority':'Pop: tonal center is unconfirmed; melody and chord fit stay primary');
}
function rnb(s,cx,lens){
 const t=tonal(s,cx,10);let b=t.bonus;
 if(['dorian','mixolydian','lydian','melodic_minor'].includes(s.id))b+=5;
 if(lens==='performance'&&pent.has(s.id))b+=19;
 if(lens==='performance'&&s.id==='blues')b+=9;
 if(bebop.has(s.id))b-=6;
 if(exotic.has(s.id)&&!cx.cadence)b-=5;
 return result(b,'R&B / soul: chord extensions, smooth voice leading and pentatonic phrasing');
}
function rock(s,cx,lens){
 const c=cx.cur;
 if(!c.intervals.some(n=>n===3||n===4)&&!cx.key)return {bonus:0,reason:'Rock: power chord without an authored key does not determine major/minor mode',uncertain:true};
 const t=tonal(s,cx,9);let b=t.bonus;
 if(['aeolian','mixolydian','dorian'].includes(s.id))b+=4;
 if(lens==='performance'&&s.id==='minor_pent')b+=23;
 if(lens==='performance'&&s.id==='blues')b+=19;
 if(lens==='performance'&&s.id==='major_pent')b+=10;
 if(exotic.has(s.id))b-=10;
 if(bebop.has(s.id))b-=13;
 if(c.family==='dominant'&&s.id==='mixolydian')b+=7;
 return result(b,'Rock / punk: riffs and pentatonic leads; chord third or authored key resolves mode');
}
function metal(s,cx,lens){
 const c=cx.cur;
 if(!c.intervals.some(n=>n===3||n===4)&&!cx.key)return {bonus:0,reason:'Metal: power chords alone cannot establish a minor, major or Phrygian mode',uncertain:true};
 const t=tonal(s,cx,12);let b=t.bonus;
 if(['aeolian','phrygian','harmonic_minor'].includes(s.id))b+=8;
 if(s.id==='dorian')b+=2;
 if(lens==='performance'&&s.id==='minor_pent')b+=14;
 if(lens==='performance'&&s.id==='blues')b+=8;
 if(s.id==='phrygian_dominant'&&cx.cadence&&cx.minorResolve)b+=7;
 if(bebop.has(s.id))b-=16;
 if(['altered','half_whole','lydian_dominant'].includes(s.id)&&!cx.cadence)b-=10;
 return result(b,'Metal: natural minor / Phrygian colors only when chord notes and key support them');
}
function blues(s,cx,lens){
 let b=0;const c=cx.cur;
 if(c.family==='dominant'&&s.id==='mixolydian')b+=17;
 if(c.family==='dominant'&&s.id==='lydian_dominant'&&!cx.sub)b-=10;
 if(lens==='performance'&&s.id==='blues')b+=46;
 if(lens==='performance'&&s.id==='blues_tonic'&&cx.blues)b+=49;
 if(lens==='performance'&&s.id==='minor_pent')b+=25;
 if(lens==='performance'&&s.id==='mixolydian')b+=7;
 if(bebop.has(s.id)&&lens!=='chord')b-=10;
 if(exotic.has(s.id)&&!cx.cadence)b-=11;
 return result(b,s.id==='blues_tonic'?'Blues: retain the song-tonic minor-blues pitches over IV/V as an expressive phrase, not a complete chord scale':lens==='performance'?'Blues: distinguish chord-root blues and song-tonic blues; neither replaces the complete dominant harmony':'Blues: dominant harmony and expressive blue notes must be evaluated separately');
}
function hiphop(s,cx,lens){
 const t=tonal(s,cx,14);let b=t.bonus;
 if(['aeolian','dorian','phrygian'].includes(s.id))b+=4;
 if(lens==='performance'&&s.id==='minor_pent')b+=22;
 if(lens==='performance'&&s.id==='blues')b+=14;
 if(lens==='performance'&&s.id==='major_pent')b+=11;
 if(bebop.has(s.id))b-=12;
 if(exotic.has(s.id)&&!cx.cadence)b-=8;
 return result(b,'Hip-hop / trap: stable sampled-key or modal loop, with economical phrase vocabulary');
}
function house(s,cx,lens){
 const t=tonal(s,cx,18);let b=t.bonus;
 if(['dorian','mixolydian','ionian','aeolian'].includes(s.id))b+=7;
 if(lens==='performance'&&pent.has(s.id))b+=12;
 if(bebop.has(s.id))b-=16;
 if(exotic.has(s.id)&&!cx.cadence)b-=12;
 return result(b,'House: coherent groove-loop key with Dorian / Mixolydian colors when supported');
}
function techno(s,cx,lens){
 const t=tonal(s,cx,20);let b=t.bonus;
 if(['aeolian','dorian','phrygian'].includes(s.id))b+=8;
 if(lens==='performance'&&s.id==='minor_pent')b+=8;
 if(bebop.has(s.id))b-=18;
 if(exotic.has(s.id)&&!cx.cadence)b-=13;
 return result(b,'Techno: repeated modal center, minimal pitch change; no automatic modal inference from a drone');
}
function trance(s,cx,lens){
 const t=tonal(s,cx,20);let b=t.bonus;
 if(['aeolian','ionian','dorian'].includes(s.id))b+=7;
 if(s.id==='harmonic_minor'&&cx.cadence&&cx.minorResolve)b+=12;
 if(lens==='performance'&&pent.has(s.id))b+=11;
 if(bebop.has(s.id))b-=17;
 if(exotic.has(s.id)&&!(cx.cadence&&cx.minorResolve))b-=12;
 return result(b,'Trance: consistent melodic key, with minor dominant color only at supported cadences');
}
function ambient(s,cx,lens){
 const t=tonal(s,cx,23);let b=t.bonus;
 if(['ionian','aeolian','dorian','lydian','mixolydian'].includes(s.id))b+=6;
 if(lens==='performance'&&pent.has(s.id))b+=9;
 if(bebop.has(s.id))b-=19;
 if(exotic.has(s.id)&&!cx.cadence)b-=14;
 return result(b,'Ambient / downtempo: sustained modal color and long-term key stability over fast re-harmonization');
}
function reggae(s,cx,lens){
 const t=tonal(s,cx,15);let b=t.bonus;
 if(['ionian','mixolydian','dorian','aeolian'].includes(s.id))b+=7;
 if(lens==='performance'&&pent.has(s.id))b+=13;
 if(bebop.has(s.id))b-=12;
 if(exotic.has(s.id)&&!cx.cadence)b-=10;
 return result(b,'Reggae / ska: tonal-center continuity; major, Mixolydian or minor colors as harmonically supported');
}
function edm(s,cx,lens){
 const t=tonal(s,cx,16);let b=t.bonus;
 if(modes.has(s.id))b+=5;
 if(lens==='performance'&&pent.has(s.id))b+=13;
 if(bebop.has(s.id))b-=17;
 if(exotic.has(s.id)&&!cx.cadence)b-=11;
 return result(b,'Electronic (general): shared tonal vocabulary with conditional chord-specific tension');
}
const engines={jazz,pop,rnb,rock,metal,blues,hiphop,house,techno,trance,ambient,reggae,edm};
function relevance(s,doc,ch,cx,lens='chord',override='auto'){
 const style=resolve(doc,override),calc=engines[style.id];
 if(!s?.pcs?.length||!cx?.cur?.supported||!calc)return {bonus:0,reason:style.neutral?'Neutral: no genre weighting':'No supported chord context',style,uncertain:!cx?.cur?.supported};
 const x=calc(s,cx,lens,style);
 let weight=Number(x.bonus)||0;
 if(s.id==='song_mode'&&cx.key){
  // Key-centered melodic playing is particularly useful for loops, riffs,
  // synth leads, and pop melodies. Never override real chord-tone conflicts.
  const preference={pop:22,rnb:16,rock:18,metal:16,hiphop:19,house:22,techno:24,trance:26,ambient:24,reggae:16,edm:20,blues:2,jazz:0};
  weight+=(lens==='performance'?(preference[style.id]||0):lens==='key'?8:-8);
 }
 // Bounded: NEVER hide authored chord or melody conflicts under style taste.
 const bonus=Math.round(Math.max(-30,Math.min(49,weight))*10)/10;
 return {bonus,reason:s.id==='song_mode'?'Authored song-key/mode retained across chord changes; track Performance only. '+x.reason:x.reason,style,uncertain:!!x.uncertain};
}
SA.genre={version:'0.32',styles,resolve,relevance,metadata,classify};
})(typeof globalThis!=='undefined'?globalThis:this);
