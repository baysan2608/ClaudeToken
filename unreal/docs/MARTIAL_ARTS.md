# Fourfold — Martial-arts movement bible (UE port)

_Architect document · 2026-10-06 · audience: the `animation` stream (clips), `character` stream (hand shapes, cloth),
`game` stream (playback rules), `fx` / `world_audio` (what each motion should look and sound like)._

Every element of Fourfold moves like a **real** Chinese martial art, so a player who knows those arts recognises them
and a player who does not still reads *weight, intent and power*. Everything here is original game design built on
public, centuries-old martial traditions and on physics; no franchise names, terms, characters, designs or scenes are
used or referenced anywhere in the project (code, assets, docs, commit text).

| Element | Martial art | One-line body idea |
|---|---|---|
| **Earth** (Stone · Metal · Sand · Magma) | **Hung Gar** (Southern Shaolin, tiger–crane) | Rooted in a deep horse stance; power comes up from the ground through the legs into short, heavy bridge-arm strikes and stomps. |
| **Water** (Water · Ice · Mist · Plant) | **Tai Chi** (Yang style, push-hands energies) | Relaxed, continuous, waist-led circles; weight shifts from one leg to the other; yield, redirect, then release with a whole-body push. |
| **Fire** (Flame · Blue · Combustion) | **Northern Shaolin** (Long Fist / Tan Tui) | Long, extended lines; explosive hip snap; long-range snapping kicks and punches; breath bursts on every strike. |
| **Fire / Lightning** | Tai Chi-sword energy work + Northern Shaolin extension | Slow circular gathering with **sword fingers**, then a sharp two-finger extension; the redirect routes the current down through the lower abdomen and out the other arm. |
| **Air** (Gust · Vortex · Vacuum · Sound) | **Baguazhang** (circle walking, palm changes) | Walk the circle, turn on hook-in / swing-out steps, spiral the body like a coiled spring, strike with open palms while evading. |

---

## 1. Shared rules for every clip

### 1.1 Game timing compresses the forms
Moves have short startups (0.10–0.30 s at 60 Hz, `docs/MOVESET.md` §7). A clip is **not** a slow form demonstration: it
keeps the *mechanics* of the technique (where the weight is, what turns first, which line the power travels) and
compresses the *time*:

* **Anticipation = the chamber / coil of the real technique** (Hung Gar sinks and loads the rear leg; Tai Chi draws the
  weight back and turns the waist; Shaolin chambers the fist at the hip; Bagua winds the torso). It ends exactly on the
  clip's **contact frame**.
* **Contact = the technique's power point** (fa jin / release): fastest velocity of the striking hand or foot, a 2-frame
  "hold" of the pose (readability on a phone), fingers locked into the hand shape.
* **Follow-through / recovery = the real return**: the body settles back into the element's stance (Earth sinks,
  Water flows on, Fire snaps back, Air keeps turning). Every clip ends on its element stance pose so cross-fades are short.
* Anticipation never exceeds the move startup: the runtime scales the clip so `contact` lands at the end of the sim
  startup (rate clamped 0.6–1.6; `ARCHITECTURE.md` §8). Author contacts near the typical startup of the slot:
  strike 0.10–0.20 s · thrust 0.17–0.23 s · ground 0.20–0.30 s · sweep 0.13–0.20 s · push 0.13–0.17 s · sink 0.07–0.13 s.
* 60 fps, every key on an integer frame, in place (no root motion: the sim moves the fighter). Contacts are frame
  numbers at 60 fps.

### 1.2 Principles of motion (what makes it read as real)
1. **Ground → legs → waist → spine → shoulder → arm → hand.** The pelvis starts turning 2–4 frames before the
   shoulders, the shoulders 1–2 frames before the hand. Never move the arm alone.
2. **Weight shift is visible.** The pelvis travels over the loading foot (5–12 cm) before a strike; the opposite foot
   lightens (heel lifts or the toes pivot). Hung Gar shifts *down*, Tai Chi shifts *back then forward*, Shaolin
   *forward*, Bagua *around*.
3. **Spirals.** Tai Chi silk reeling and Bagua coiling: forearms rotate (palms turn over) while the arm extends; the
   twist bones (`*_twist_01/02`) carry 30 % / 60 % of the wrist roll so the forearm deforms well.
4. **Breathing.** Stances breathe (spine_03–05 and clavicles rise 1–2 cm over 1.5–2.5 s). Strikes exhale: chest
   compresses at contact, shoulders drop (Hung Gar adds a sharp exhale "shout" pose; Shaolin a short burst; Tai Chi a
   long, soft exhale; Bagua a smooth continuous breath).
5. **Overlap and settle.** Head and neck lag the torso by 1–2 frames; the free hand settles 2–3 frames after the
   striking hand; hips settle with a small overshoot (2–3 cm) on heavy techniques.
6. **Feet are planted unless the technique steps.** Planted feet must not slide (authoring IK keeps the ball and heel
   fixed); a step lifts the heel first and lands heel- or ball-first according to the style (Hung Gar: whole foot,
   heavy; Tai Chi: heel first, weight follows; Shaolin: ball first, springy; Bagua: flat "mud-wading" glide).
   Mark planted ranges in `clips.json` (`foot_plants`) for the runtime foot IK.
7. **Charge reads in the body.** Tier 1: the chamber deepens (pelvis 3 cm lower, fists tighter). Tier 2: a visible
   tremble (±0.3° at 12 Hz on the arms) and a breath hold. Tier 3: the full "ready" posture of the style (Hung Gar
   iron-wire tension, Tai Chi stillness with the ball held, Shaolin fists at the hips, Bagua wound to the maximum).
   Hold loops (`*_charge`, `*_hold`) must loop seamlessly and stay readable from 10 m on a phone.

### 1.3 Stances (all styles)
| Stance | Chinese | Feet | Weight | Used by |
|---|---|---|---|---|
| Horse | 四平馬 sei ping ma / 馬步 ma bu | parallel, 1.6–2 shoulder widths, thighs near horizontal (game: 70 % depth) | 50 / 50 | Earth stance, Fire charge, heavy guards |
| Bow | 弓步 gong bu | front knee over ankle, rear leg straight, rear foot 45° out, ~3.5 shoe lengths | 70 front | every push / punch release |
| Empty (cat) | 虛步 xu bu | front toes touch, rear leg bent | 100 rear | Tai Chi ready, Shaolin chamber, evasive retreats |
| Drop | 仆步 pu bu | one leg fully bent, the other straight along the floor | over the bent leg | Tai Chi "Snake creeps down", low sweeps |
| Crossed / rest | 歇步 xie bu | legs crossed, sitting low | rear | Bagua turns, Shaolin spin recoveries |
| Hook-in / swing-out steps | 扣步 kou bu / 擺步 bai bu | toe turned in / out | moving | Bagua pivots and palm changes |
| Mud-wading step | 趟泥步 tang ni bu | foot glides flat just above the floor | moving | Bagua circle walking (Air locomotion flavour) |
| Pigeon-toed stance | 二字鉗羊馬 (yee jee kim yeung ma) | knees in, toes in | 50 / 50 | Metal guard (iron stance) |

### 1.4 Hand shapes (`hand_*` single-frame clips; the runtime overrides finger bones per move)
| Clip | Shape | Style / use |
|---|---|---|
| `hand_fist` | standard fist, thumb over index/middle | Shaolin punches, Hung Gar fists |
| `hand_palm` | flat, fingers together, wrist cocked back 30° | Tai Chi push (An), Hung Gar palms |
| `hand_willow` | "fair lady's hand": soft palm, fingers slightly separated and curved | Tai Chi flow, Water / Mist / Plant |
| `hand_tiger` | tiger claw (虎爪): fingers spread, every joint curled, palm heel forward | Hung Gar strikes, Earth / Sand / Magma |
| `hand_crane` | crane beak (鶴嘴): fingertips pinched to a point, wrist bent | Hung Gar flicks, Metal discs, Tai Chi single-whip hook |
| `hand_sword` | sword fingers (劍指): index + middle extended together, ring + pinky curled under the thumb | Lightning, Blue, Combustion pointing |
| `hand_oxtongue` | ox-tongue palm (牛舌掌): fingers together, thumb tucked, slight cup, palm up or forward | Baguazhang, every Air move |
| `hand_relaxed` | natural half-open | idle, locomotion |
| `hand_cup` | rounded as if holding a ball | Tai Chi "hold the ball", technique holds |
| `hand_spread` | fingers wide, tense | catches, magnet grip, absorbing |

---

## 2. Style bibles

### 2.1 Earth — Hung Gar (Southern Shaolin)
* **Body:** low centre of gravity, wide horse stance; the spine stays upright; power is *short and heavy*. Forearms are
  "bridges" (橋手 kiu sau) that meet force and drive through it; the iron-wire form (鐵線拳) adds slow dynamic tension
  with breathing sounds — use it for guards and holds.
* **Signature techniques used:** tiger claw (虎爪 fu jow), crane beak (鶴嘴 hok jui), iron bridge forearm drive
  (鐵橋), double tiger palms push, hanging/back fist (掛捶 gwa choi), stomps that shake the ground, the "shadowless
  kick" (無影腳 mo ying geuk: a low snap kick hidden behind the hands).
* **Rhythm:** sink (load) — hold one frame — explode upward/forward — sink again. Every Earth strike starts with the
  pelvis dropping 4–6 cm; every Earth guard settles with a small bounce (heavy).
* **Stomp:** knee rises to hip height over 6–8 frames, foot slams flat in 2 frames, both knees absorb 3 cm, a 1-frame
  shoulder shudder. The stomp is the Earth "telegraph" for moves that raise material from the ground.

### 2.2 Water — Tai Chi (Yang style)
* **Body:** relaxed (鬆 song), upright, the waist leads every movement; arms stay rounded (never locked); weight
  shifts "empty / full" between the legs; movement is continuous — no full stops except the contact hold.
* **Signature techniques used (24-form postures and push-hands energies):** Commencement, Part the Wild Horse's Mane,
  White Crane Spreads Its Wings, Brush Knee and Push, Hands Play the Pipa, Repulse the Monkey, Grasp the Bird's Tail
  (Ward Off 掤 peng, Roll Back 捋 lü, Press 擠 ji, Push 按 an), Single Whip (hook hand), Cloud Hands, Needle at Sea
  Bottom, Fan Through the Back, Snake Creeps Down, Separate Foot / Heel Kick, Fair Lady Works the Shuttles; silk-reeling
  spirals (纏絲勁) and fa jin bursts for strikes.
* **Rhythm:** draw back (weight to the rear leg, waist turns away) → the circle continues through the bottom → release
  forward with the weight. Water "lashes" are Part-the-Mane arcs; Water "pushes" are An; Ice adds a **crisp stop**
  (fists clench, a 3-frame freeze at contact); Mist is the softest and floatiest (longer settles, slower hands); Plant
  adds a whipping wrist and grasping hands (pluck 採 cai).

### 2.3 Fire — Northern Shaolin (Long Fist, Tan Tui)
* **Body:** long extended lines (the whole arm/leg reaches), explosive hip snap, springy feet, fast retraction (a
  punch returns to the hip as fast as it left). Breath bursts on every strike (a 2-frame chest compression).
* **Signature techniques used:** straight punch (衝拳 chong quan), pushing palm (推掌 tui zhang), Tan Tui snap kick
  (彈腿), heel kick (蹬腳 deng jiao), low spinning sweep (掃膛腿 sao tang tui), outside crescent kick (擺蓮), tornado
  kick (旋風腳 xuan feng jiao), jump front kick, bow-stance / horse-stance switches, double palms out of the horse.
* **Rhythm:** chamber at the hip (fist palm-up) → hip snaps → strike fully extended → snap back. Fire throws its
  flames from the **striking limb at full extension** (fist, palm, or the foot of a kick), which is why Fireball and
  Fire Line are kicks.
* **Blue fire:** the same style made precise: sword fingers, narrow lines, minimal wind-up, longer stillness at
  contact (concentrated heat). **Combustion:** pointing with sword fingers, then a sharp fist-snap (the detonation).

### 2.4 Fire / Lightning — circular charge, two-finger release
* **Charge loop:** feet in a shallow horse; both arms trace slow, opposite circles in front of the body (a Tai Chi
  sword "gathering" form), fingers in sword shape; the weight shifts left-right with each circle; the hands separate
  and come back (the "separating" visual). Tier ups: the circles shrink and speed up, the stance lowers, fingers tremble.
* **Release:** step into a bow stance, lead arm extends straight, two fingers point at the target, rear hand pulls back
  to the hip (counter-balance); 2-frame hold, then a fast snap back.
* **Redirect (perfect Fire guard vs a bolt):** the lead hand's fingertips meet the bolt, the arm draws it **down
  across the lower abdomen (dan tian)** in a half circle while the weight sinks and the waist turns, and the *other*
  arm extends to send it back out — a 36-frame clip with two contacts (catch, release).
* **Skybreak:** the lead arm rises straight up with sword fingers, a held 6-frame pause at the top, then the arm
  slashes down toward the target as the knee drops.

### 2.5 Air — Baguazhang
* **Body:** the torso is always twisted toward the "centre" (the opponent) while the feet circle; palms are open
  (ox-tongue palm); the body coils and uncoils like a spring; evasion and attack are one motion.
* **Signature techniques used:** circle walking (走圈) with the mud-wading step (趟泥步), Single Palm Change (單換掌),
  Double Palm Change (雙換掌), the Eight Mother Palms (turning body, smooth flow, back body, circle body ...), hook-in /
  swing-out steps (扣步 / 擺步), piercing palm (穿掌 chuan zhang), pushing palm (推掌), lifting palm, pluck (採).
* **Rhythm:** wind (torso turns away, kou bu) → the palm change unwinds the body → the palm strikes at the end of the
  turn → keep walking. Air strikes push air with open palms; Vortex adds full spins on the ball of one foot; Vacuum adds
  pulling (pluck, drawing the palms back to the body); Sound adds percussive claps and a chest-expanding shout.

---

## 3. Clip catalogue (the `animation` stream's deliverable)

Names are final (`clips.json` keys; assets `A_<name>`). `P0` = needed for the first playable build, `P1` = every slot
of every element reads correctly, `P2` = specials. Frames at **60 fps**; `c` = contact frame; loops: last frame equals
the first. Every clip starts and ends on its base stance (column "base").

### 3.1 Shared: locomotion, reactions, modes, hands
| clip | P | frames | loop | c | base | content / keyframe notes |
|---|---|---|---|---|---|---|
| `idle` | P0 | 120 | ✓ | – | idle | 無極 wu ji ready: feet shoulder width, knees soft, hands relaxed, breathing 2 s |
| `walk` | P0 | 54 | ✓ | – | idle | 1.4 m/s, stride 1.26 m per cycle, arms swing 15° (design speed in clips.json `speed`) |
| `run` | P0 | 36 | ✓ | – | idle | 5.5 m/s, stride 3.3 m, 12° lean, hips dip 6 cm |
| `strafe_l` / `strafe_r` | P0 | 54 | ✓ | – | guard | 1.1 m/s, guard up, feet never cross |
| `walk_back` | P0 | 54 | ✓ | – | guard | 1.0 m/s, toe first, guard up |
| `turn_l90` / `turn_r90` | P2 | 30 | | 18 | idle | in-place pivot steps |
| `evade_l` / `evade_r` / `evade_back` / `evade_fwd` | P0 | 28 | | 12 | idle | low sidestep / hop; contact = apex (centre of i-frames) |
| `jump` | P0 | 18 | | 10 | idle | crouch → take-off at contact, ends rising |
| `fall` | P0 | 48 | ✓ | – | – | airborne, arms balancing |
| `land` | P0 | 18 | | 4 | idle | heels-then-knees absorb, rise |
| `hit_light_front` | P0 | 22 | | 3 | idle | head and chest snap back 8°, arms jerk |
| `hit_light_back` | P1 | 22 | | 3 | idle | arches forward from a hit behind |
| `hit_heavy` | P0 | 36 | | 6 | idle | thrown back, front foot leaves the floor, stumble, recover |
| `knockdown` | P0 | 42 | | 4 | – | falls back, ground slam ~f34, ends lying |
| `getup` | P0 | 48 | | – | idle | lying → kip / roll to knee → stand (martial "carp" kip-up for Fire, roll for others is P2) |
| `stagger` | P0 | 30 | | 6 | idle | off-balance wobble, one foot replants |
| `guard_break` | P1 | 36 | | 6 | idle | guard knocked open, both arms fly wide, backstep |
| `block_impact` | P0 | 16 | | 2 | guard | short shudder used additively on any guard |
| `deflect` | P0 | 22 | | 5 | guard | generic perfect parry (cross-body sweep, hip turn) |
| `salute` | P1 | 60 | | – | idle | fist-and-palm salute (抱拳禮): left palm covers right fist at chest, slight bow |
| `glide` | P0 | 96 | ✓ | – | – | arms spread, body pitched forward, legs trailing |
| `flight` | P1 | 72 | ✓ | – | – | upright flight, palms down pressing the air (Sound flight) |
| `hover` | P1 | 72 | ✓ | – | – | lotus-like hover, slow arm floats |
| `skate` | P1 | 48 | ✓ | – | – | ice skating push-glide (Ice Skate) |
| `surf` | P1 | 48 | ✓ | – | – | riding a wave/dune, knees bent, arms balancing (Sand ride, Wave ride) |
| `hand_fist` … `hand_spread` | P0 | 1 | | – | – | the ten hand shapes of §1.4 (finger bones only) |

### 3.2 Earth (Hung Gar) — prefix `e_`
| clip | P | frames | c | technique | keyframe notes |
|---|---|---|---|---|---|
| `e_stance` | P0 | 120 loop | – | Sei ping ma, double tiger claws | hands at chest and hip level, heavy 2 s breath, tiny knee pulses |
| `e_lift` | P0 | 24 | 12 | stomp + rising scoop | stomp f4–6, palms turn up and rise from knee to chest by c (the stone pops at contact) |
| `e_strike` | P0 | 24 | 8 | iron-bridge drive (鐵橋) | step into bow stance, rear forearm/fist drives straight at chest height, hips square at c |
| `e_heave` | P0 | 36 | 18 | heave overhead → drive down | both hands under the mass at the knees, lift overhead (back arches 10°), drive forward-down into a deep bow at c |
| `e_thrust` | P1 | 24 | 12 | tiger claw thrust (虎爪) | rear claw chambers at the hip, thrusts forward in bow stance, claw opens at c |
| `e_ground_rise` | P1 | 30 | 15 | lifting the bridge | drop into horse, both palms up from the ground to chest (spikes rise) |
| `e_ground_slap` | P1 | 30 | 14 | hammer palm to the ground | sink low, right palm winds up and slaps the floor ahead of the lead foot |
| `e_sweep` | P1 | 30 | 13 | crane wing sweep (鶴翼) | waist pivots 70°, rear arm sweeps horizontally at chest height, crane beak at the end |
| `e_wall` | P0 | 24 | 10 | stomp + double palms up | Bulwark rise: stomp at f4, palms rise to face height at c, elbows down |
| `e_guard` | P0 | 96 loop | – | bridge-arm guard (橋手) | forearms crossed at chest in horse stance, iron-wire tension breathing |
| `e_push` | P1 | 30 | 10 | double tiger palms push | lunge into bow, both palms drive the wall forward at shoulder height |
| `e_sink` | P1 | 24 | 8 | pressing the earth | palms press down from chest to knee, stance lowers 6 cm (Swallow / Quicksand / Melt Pit) |
| `e_seize_loop` | P0 | 72 loop | – | embrace the mountain | cupped hands round a heavy mass at chest, strained tremble |
| `e_throw` | P0 | 24 | 8 | straight push / crane-beak release | from the hold, both palms push the mass away (or right-hand flick when light) |
| `e_overhead_slam` | P1 | 36 | 18 | double hammer down | rise on the toes with both fists overhead, crash down into a deep horse (Rod Plant) |
| `e_disc_flick` | P1 | 20 | 8 | crane-beak flick | cross-body backhand flick (Razor Disc, Plate Rush) |
| `e_chain_whirl` | P1 | 36 | 14 | hanging back-fist arc (掛捶) | circular arm arc 120° (T3 repeats into 360°) |
| `e_lob` | P1 | 24 | 12 | underhand scoop toss | magma globs / bombs lobbed upward |
| `e_pour` | P0 | 30 | 16 | press the mountain down | raise the molten mass, press it down and sweep forward along the ground (Magma Surge, Slag Wave) |
| `e_magma_hold` | P1 | 72 loop | – | magma hold | wide stance, cupped hands, fast heat tremble |
| `e_spatter` | P2 | 30 | 12 | low crescent arm spray | low sweeping arm throwing droplets |
| `e_burrow` | P1 | 28 | 14 | sink and shoot | drop low, slide-step, rise |
| `e_stone_skin` | P1 | 96 loop | – | iron-wire tension (鐵線) | rooted horse, arms in tension, rhythmic breath; Metal uses the pigeon-toed stance variant |
| `e_shadowless_kick` | P2 | 30 | 12 | 無影腳 low snap kick | hidden low kick (Sand Surf tap / combos) |

### 3.3 Water (Tai Chi) — prefix `w_`
| clip | P | frames | c | technique | keyframe notes |
|---|---|---|---|---|---|
| `w_stance` | P0 | 144 loop | – | ward-off ready (掤) | slow circling hands, weight shifting rear↔front over 2.4 s |
| `w_lash` | P0 | 30 | 14 | Part the Wild Horse's Mane | diagonal upward arc of the lead arm with a step-through, waist turn 60° |
| `w_freeze` | P0 | 24 | 14 | Hands Play the Pipa → clench | reach open, both hands close sharply, 3-frame freeze at c (Ice) |
| `w_push` | P0 | 30 | 12 | Push (按 an) | sit back, palms drop, then full weight transfer with double palms |
| `w_press` | P1 | 24 | 10 | Press (擠 ji) | palms overlapped at the chest press forward (Water Bullet, Fog Lance) |
| `w_maelstrom` | P2 | 48 | 24 | turning whip | 360° turn with the arm trailing (Maelstrom Lash, Briar Storm) |
| `w_ground` | P1 | 36 | 18 | Needle at Sea Bottom → rising push | dip the hand to the floor, rise and push forward (Tidal Rush, Rime Path, Root Snare) |
| `w_single_whip` | P1 | 30 | 14 | Single Whip (單鞭) | hook hand behind, lead palm opens out wide (Spray / Hoarfrost / Thicket Fan) |
| `w_shield` | P0 | 96 loop | – | Cloud Hands (雲手) | continuous double circles in front of the body (Water Shield, Steam Screen, Lattice) |
| `w_snake` | P1 | 30 | 14 | Snake Creeps Down (下勢) | drop stance, lead hand traces the floor (Slick, Frost Floor, Dew Fall) |
| `w_draw` | P0 | 30 | 22 | Roll Back (捋 lü) | sweep from low-back to high-front, gathering (Draw & Shape start) |
| `w_hold` | P0 | 96 loop | – | Hold the Ball (抱球) | hands circling a sphere, opposite phase |
| `w_release` | P0 | 24 | 8 | Push release | the gathered water is pushed out as a stream |
| `w_clench` | P1 | 18 | 10 | T+A freeze | sharp double clench (Freeze shape) |
| `w_heel_kick` | P1 | 30 | 14 | Separate Foot / heel kick (蹬腳) | arms open, heel drives forward (Glacier Shove) |
| `w_crane` | P2 | 30 | 15 | White Crane Spreads Its Wings | rising arms, empty stance (Veil, Creeping Fog) |
| `w_repulse` | P1 | 28 | 12 | Repulse the Monkey (倒捲肱) | back-step with palm push (Riptide Step, Mist Step) |
| `w_shuttle` | P2 | 30 | 14 | Fair Lady Works the Shuttles | rising forearm + push (Lattice Roll, Vinegrip release) |

### 3.4 Fire (Northern Shaolin) — prefix `f_`; Lightning `l_`; Combustion `c_`
| clip | P | frames | c | technique | keyframe notes |
|---|---|---|---|---|---|
| `f_stance` | P0 | 72 loop | – | ready stance, springy | fists up, bounce twice per loop |
| `f_jab` | P0 | 18 | 6 | 衝拳 lead straight punch | hip snap, full extension, fast retraction |
| `f_cross` | P0 | 24 | 8 | rear straight punch / palm | bow stance, full hip rotation |
| `f_charge` | P0 | 48 loop | – | horse stance, fists at hips | breath gathering, tremble from T2 |
| `f_palm_burst` | P0 | 24 | 6 | double palms out of the horse | release from the charge (Blaze, Backdraft, Flash Over, Shockwave) |
| `f_column` | P1 | 30 | 12 | rising uppercut palm | exhale, palm rises past the face (Fire Column) |
| `f_inferno` | P1 | 48 | 26 | circle + double palm | arms trace a full circle, sink into horse, double palms (Inferno, Nova) |
| `f_snap_kick` | P0 | 24 | 10 | 彈腿 Tan Tui snap kick | knee chambers, foot snaps out at waist height — the fireball leaves the foot |
| `f_low_sweep` | P1 | 36 | 16 | 掃膛腿 low spinning sweep | drop stance, rear leg sweeps the floor 180° (Fire Line, Blue Furrow) |
| `f_crescent_kick` | P1 | 30 | 14 | 擺蓮 outside crescent | leg arcs across the front (Fire Fan T0–T2, Corona) |
| `f_tornado_kick` | P2 | 48 | 28 | 旋風腳 tornado kick | jump spin, inside crescent at the top (Fire Fan T3 / Nova) |
| `f_stomp` | P1 | 24 | 10 | stomp + palms down | Ground Heat, Chain Blasts, Grounding |
| `f_heat_draw` | P0 | 96 loop | – | rooted absorbing | palms pull heat toward the chest, then push out (DRAW, Smelter channel) |
| `f_thermal_hold` | P0 | 72 loop | – | magma grip | cupped hands, fast tremble (HEAT) |
| `f_pour` | P0 | 30 | 16 | pour | raise, press down, sweep along the ground |
| `f_dash` | P1 | 24 | 8 | lunge dash | Flare Dash, Shimmer Step |
| `f_hop` | P1 | 30 | 12 | jump with palms down | Rocket Hop, Blast Jump, Smother Blast |
| `f_needle` | P1 | 20 | 8 | sword-finger thrust | Blue Needle / Lance / Searing Beam, Comet Flame |
| `f_corona` | P2 | 36 | 18 | arms-extended spin | Corona ring |
| `l_charge` | P0 | 96 loop | – | circular gathering, sword fingers | §2.4 charge loop |
| `l_release` | P0 | 20 | 6 | two-finger extension | §2.4 release (Bolt, Rail Arc, Spark T0 is `f_jab` with sword fingers) |
| `l_redirect` | P1 | 36 | 10 (catch), 26 (release) | redirect through the lower abdomen | §2.4 redirect (Return Current) |
| `l_skybreak` | P1 | 36 | 24 | sky strike | §2.4 Skybreak |
| `l_ground` | P1 | 30 | 14 | palm to the ground | Ground Current |
| `l_fan` | P2 | 30 | 12 | fingers-spread arc | Arc Fan |
| `c_point` | P1 | 24 | 10 | point → fist snap | Pop / Burst / Blast / Detonation, Fuse release |
| `c_toss` | P2 | 20 | 8 | ember flick | Spark Mine, Scatter Charges |
| `c_fuse_loop` | P2 | 72 loop | – | focus with sword fingers | Fuse channel |
| `c_chain_stomp` | P2 | 60 | 12 / 24 / 36 | stepping stomps | Chain Blasts |

### 3.5 Air (Baguazhang) — prefix `a_`
| clip | P | frames | c | technique | keyframe notes |
|---|---|---|---|---|---|
| `a_stance` | P0 | 144 loop | – | Bagua guard, dragon posture | torso turned to the centre, lead ox-tongue palm at eye level, rear palm at the elbow, tiny circle steps |
| `a_palm` | P0 | 22 | 8 | single pushing palm (推掌) | waist twist, palm turns over as it extends |
| `a_double_palm` | P0 | 30 | 12 | Double Palm Change (雙換掌) | wind, change, double palms (Cyclone, Gale, Air Cannon) |
| `a_hurricane` | P1 | 48 | 28 | kou-bu turn + double palm drive | full turn, the palms drive at the end (Hurricane Palm, Collapse) |
| `a_pierce` | P1 | 24 | 10 | piercing palm (穿掌) | lead palm slides over the rear forearm and pierces forward (Wind Crescent, Spiral Lance, Sound Lance) |
| `a_low_palm` | P1 | 30 | 14 | swallow skims the water | low drop, palm sweeps along the floor (Dust Devil Line, Dust Funnel, Pressure Mine) |
| `a_turn_palm` | P1 | 36 | 16 | turning-body palm (kou bu / bai bu) | horizontal palm sweep while pivoting (Crosswind, Vacuum Arc) |
| `a_wall_push` | P1 | 30 | 12 | big two-palm push with step | Wall of Wind, Unleash, Pressure Wave |
| `a_downdraft` | P1 | 24 | 10 | palms press down from above | Downdraft, Funnel Down, Anchor |
| `a_updraft` | P0 | 30 | 14 | spring jump, rising palms | Updraft, Pressure Hop, Flight start |
| `a_spin` | P1 | 36 | 18 | 360° turn on one foot | Twister, Tornado, Eddy Ring, Spin Step |
| `a_circle_walk` | P1 | 96 loop | – | 趟泥步 circle walking | Tailwind / Slipstream run flavour, Air idle variation |
| `a_dash` | P0 | 18 | 8 | streamlined dash | Air Dash, Boom Step, Thunder Step |
| `a_gather` | P1 | 48 | 30 | wide gathering circle overhead | Eye of the Storm, Vacuum Well |
| `a_pluck` | P1 | 24 | 10 | pluck (採) — pull to the body | Suction Line |
| `a_guard` | P0 | 96 loop | – | circling palms guard | Wind Guard, Vortex Wall, Null Bubble, Sound Barrier |
| `a_rising_guard` | P2 | 24 | 10 | rising crossed forearms | Vortex Wall raise |
| `a_clap` | P1 | 18 | 6 | sharp clap | Clap |
| `a_roar` | P1 | 36 | 14 | chest-expanding shout | Shout, Roar, Resonance |

---

## 4. Move → technique → clip map (all 160 slots)

Columns: slot · move (sim id) · real technique · clips: `startup` (time-scaled so its contact lands at the end of the
sim startup) → `hold` loop (charge / channel, if any) → `release` (if different) · hands (L/R).
The `animation` stream writes these into `Content/Fourfold/Data/anim_map.json` (`ARCHITECTURE.md` §8.3) and may refine
them; any change must keep the slot's timing class.

### 4.1 Earth / Stone (Hung Gar, rooted)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `earth_attack` Stone Shot→Crag Breaker | stomp-lift then iron-bridge drive; T1+ heave | `e_lift` → `e_strike` (T1+: `e_heave`) | tiger / fist |
| thrust | `spear_stone` | tiger claw thrust | `e_thrust` | tiger |
| ground | `rising_fangs` | lifting the bridge | `e_ground_rise` | palm |
| sweep | `rubble_fan` | crane wing sweep | `e_sweep` | crane |
| guard | `guard` (Bulwark) | stomp + double palms, bridge guard | `e_wall` → `e_guard` | palm |
| push | `ram_wall` | double tiger palms push | `e_push` | tiger |
| sink | `swallow` | pressing the earth | `e_sink` | palm |
| tech | `earth_tech` Seize / Split | embrace the mountain | `e_seize_loop` → `e_throw` | spread / palm |
| evade | `evade` | low sidestep | `evade_*` | relaxed |
| evade_hold | `stone_skin` | iron-wire tension | `e_stone_skin` | fist |

### 4.2 Earth / Metal (Hung Gar crane hands, iron wire)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `razor_disc` | crane-beak backhand flick | `e_disc_flick` | crane |
| thrust | `iron_lance` | tiger claw thrust (javelin line) | `e_thrust` | crane |
| ground | `lodestone_line` | hammer palm to the ground | `e_ground_slap` | palm |
| sweep | `chain_arc` | hanging back-fist arc | `e_chain_whirl` | fist |
| guard | `aegis_plate` | bridge guard holding the plate | `e_guard` | spread |
| push | `plate_rush` | crane-beak flick | `e_disc_flick` | crane |
| sink | `rod_plant` | double hammer down | `e_overhead_slam` | fist |
| tech | `lodestone_grip` | magnet grip (spread hands) | `e_seize_loop` → `e_throw` | spread |
| evade | `magnet_glide` | pulled glide | `a_dash` | relaxed |
| evade_hold | `iron_stance` | pigeon-toed iron stance | `e_stone_skin` | fist |

### 4.3 Earth / Sand (Hung Gar low, sweeping)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `grit_shot` | stomp-lift, drive | `e_lift` → `e_strike` | tiger |
| thrust | `sandblast` | iron-bridge push held (jet) | `e_thrust` (held) | palm |
| ground | `sand_surge` | hammer palm to the ground | `e_ground_slap` | palm |
| sweep | `veil_of_grit` | crane wing sweep | `e_sweep` | crane |
| guard | `dune_wall` | stomp + palms, bridge guard | `e_wall` → `e_guard` | palm |
| push | `dune_push` | double tiger palms | `e_push` | tiger |
| sink | `quicksand` | pressing the earth | `e_sink` | palm |
| tech | `sandform` | gather and compress | `e_seize_loop` → `e_throw` | cup |
| evade | `sand_surf` | low slide | `e_burrow` | relaxed |
| evade_hold | `sand_ride` | riding the dune | `surf` | relaxed |

### 4.4 Earth / Magma (Hung Gar heavy, pressing)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `ember_clot` | underhand scoop toss | `e_lob` | cup |
| thrust | `lava_lash` | back-fist arc whip | `e_chain_whirl` | fist |
| ground | `magma_surge` | press the mountain down | `e_pour` | palm |
| sweep | `spatter_arc` | low crescent arm spray | `e_spatter` (fallback `e_sweep`) | spread |
| guard | `magma_curtain` | stomp + palms | `e_wall` → `e_guard` | palm |
| push | `slag_wave` | press down and push | `e_pour` | palm |
| sink | `melt_pit` | pressing the earth | `e_sink` | palm |
| tech | `magma_hold` | magma hold | `e_magma_hold` → `e_pour` | cup |
| evade | `cinder_step` | low dash | `evade_fwd` | relaxed |
| evade_hold | `lava_wade` | heavy wading walk | `walk` (slow) | relaxed |

### 4.5 Water / Water (Tai Chi)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `water_attack` Lash→Ice Lance→Torrent→Maelstrom | Part the Mane; T1 Pipa clench; T2 Push; T3 turning whip | `w_lash` (T1 `w_freeze`, T2 `w_push`, T3 `w_maelstrom`) | willow / fist (T1) / palm |
| thrust | `water_bullet` | Press (ji) | `w_press` | palm |
| ground | `tidal_rush` | Needle at Sea Bottom → push | `w_ground` | palm |
| sweep | `spray_fan` | Single Whip | `w_single_whip` | willow / crane (hook) |
| guard | `guard` (Water Shield) | Cloud Hands | `w_shield` | willow |
| push | `surge_orb` | Push (an) | `w_push` | palm |
| sink | `slick` | Snake Creeps Down | `w_snake` | willow |
| tech | `water_tech` Draw & Shape | Roll Back → Hold the Ball → Push | `w_draw` → `w_hold` → `w_release` (T+A `w_clench`) | cup |
| evade | `riptide_step` | Repulse the Monkey | `w_repulse` | willow |
| evade_hold | `wave_ride` | riding the wave | `surf` | willow |

### 4.6 Water / Ice (Tai Chi with crisp stops)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `frost_shard` | Pipa → clench | `w_freeze` | fist |
| thrust | `icicle_volley` | Press with snapping fingers | `w_press` | sword |
| ground | `rime_path` | Needle at Sea Bottom (palm skims) | `w_ground` | palm |
| sweep | `hoarfrost_fan` | Single Whip, freezing breath | `w_single_whip` | willow |
| guard | `ice_wall` | clench → Cloud Hands | `w_freeze` → `w_shield` | palm |
| push | `glacier_shove` | Separate Foot heel kick | `w_heel_kick` | palm |
| sink | `frost_floor` | Snake Creeps Down | `w_snake` | palm |
| tech | `freeze_draw` | Roll Back → hold → push (T+A shatter) | `w_draw` → `w_hold` → `w_release` | cup |
| evade | `ice_glide` | gliding step | `evade_*` | willow |
| evade_hold | `skate` | skating | `skate` | relaxed |

### 4.7 Water / Mist (Tai Chi, softest)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `scald_puff` | Brush Knee and Push (short) | `w_push` (fast) | willow |
| thrust | `fog_lance` | Press | `w_press` | willow |
| ground | `creeping_fog` | White Crane / low sweep | `w_crane` (fallback `w_ground`) | willow |
| sweep | `veil` | Cloud Hands opening | `w_single_whip` | willow |
| guard | `steam_screen` | Cloud Hands | `w_shield` | willow |
| push | `steam_blast` | Push | `w_push` | palm |
| sink | `dew_fall` | Snake Creeps Down | `w_snake` | willow |
| tech | `vapor_draw` | Roll Back → hold the ball | `w_draw` → `w_hold` → `w_release` | cup |
| evade | `mist_step` | Repulse the Monkey | `w_repulse` | willow |
| evade_hold | `fog_walk` | slow walk | `walk` (slow) | willow |

### 4.8 Water / Plant (Tai Chi with plucking, whipping wrists)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `bramble_lash` | Part the Mane (whipping wrist) | `w_lash` (T3 `w_maelstrom`) | crane |
| thrust | `burr_shot` | Press with a flick | `w_press` | crane |
| ground | `root_snare` | Needle at Sea Bottom | `w_ground` | spread |
| sweep | `thicket_fan` | Single Whip | `w_single_whip` | spread |
| guard | `living_lattice` | Cloud Hands | `w_shield` | spread |
| push | `lattice_roll` | Fair Lady Works the Shuttles | `w_shuttle` (fallback `w_push`) | palm |
| sink | `deep_roots` | rooting sink | `w_snake` | spread |
| tech | `vinegrip` | pluck (cai) and hold | `w_draw` → `w_hold` → `w_release` | spread |
| evade | `vine_swing` | swing step | `evade_*` | crane |
| evade_hold | `canopy` | hang / hover | `hover` | relaxed |

### 4.9 Fire / Flame (Northern Shaolin)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `fire_attack` Flare→Blaze→Column→Inferno | jab; T1 charge → palm burst; T2 rising palm; T3 circle + double palm | `f_jab` (charge `f_charge`; T1 `f_palm_burst`, T2 `f_column`, T3 `f_inferno`) | fist / palm |
| thrust | `fireball` | Tan Tui snap kick | `f_snap_kick` | fist |
| ground | `fire_line` | low spinning sweep | `f_low_sweep` | fist |
| sweep | `fire_fan` | crescent kick (T3 tornado kick) | `f_crescent_kick` (T3 `f_tornado_kick`) | palm |
| guard | `flame_guard` | guard / perfect parry | `guard` → `deflect` | palm |
| push | `backdraft` | double palms from the horse | `f_palm_burst` | palm |
| sink | `ground_heat` | stomp + palms down | `f_stomp` | palm |
| tech | `fire_tech` Thermal HEAT/DRAW/VENT/SCORCH | magma grip / heat draw / pour | HEAT `f_thermal_hold` → `f_pour`; DRAW / SCORCH `f_heat_draw`; VENT `f_palm_burst` | cup / spread |
| evade | `evade` | sidestep | `evade_*` | fist |
| evade_hold | `rocket_hop` | jump, palms down | `f_hop` | palm |

### 4.10 Fire / Blue (Shaolin made precise)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `blue_needle` | sword-finger thrust (T2+ held beam) | `f_needle` (charge `f_charge`) | sword |
| thrust | `comet_flame` | snap kick, tight | `f_snap_kick` | sword |
| ground | `blue_furrow` | low sweep | `f_low_sweep` | sword |
| sweep | `corona` | arms-extended spin | `f_corona` (fallback `f_crescent_kick`) | sword |
| guard | `blue_aegis` | guard / parry | `guard` → `deflect` | palm |
| push | `flash_over` | double palm burst | `f_palm_burst` | palm |
| sink | `kiln` | focused heat on a body | `f_heat_draw` | sword |
| tech | `smelter` | ranged heat channel | `f_heat_draw` | sword |
| evade | `shimmer_step` | lunge dash | `f_dash` | sword |
| evade_hold | `afterburn` | run | `run` | fist |

### 4.11 Fire / Lightning (circular charge, two-finger release)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `spark` Spark→Bolt→Storm Bolt→Skybreak | T0 sword-finger jab; T1+ circular charge → two-finger release; T3 sky strike | T0 `f_jab`; charge `l_charge` → `l_release` (T3 `l_skybreak`) | sword |
| thrust | `rail_arc` | two-finger extension | `l_release` | sword |
| ground | `ground_current` | palm to the ground | `l_ground` | palm |
| sweep | `arc_fan` | fingers-spread arc | `l_fan` (fallback `l_release`) | spread |
| guard | `static_ward` Static Ward / Return Current | guard; perfect = redirect | `guard` → `l_redirect` | sword |
| push | `static_burst` | double palm burst | `f_palm_burst` | spread |
| sink | `grounding` | stomp, palms to earth | `f_stomp` | palm |
| tech | `conductors_hand` | charge a conductor, release | `l_charge` → `l_release` | sword |
| evade | `arc_step` | flash step | `f_dash` | sword |
| evade_hold | `overcharge` | crackling run | `run` | sword |

### 4.12 Fire / Combustion (pointing, fist snap)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `pop` Pop→Burst→Blast→Detonation | palm pop; T1+ point → fist snap | T0 `f_jab` (palm); T1+ `c_point` | sword → fist |
| thrust | `spark_mine` | ember flick | `c_toss` (fallback `e_disc_flick`) | crane |
| ground | `chain_blasts` | stepping stomps | `c_chain_stomp` (fallback `f_stomp`) | fist |
| sweep | `scatter_charges` | fanned ember flick | `c_toss` | spread |
| guard | `reactive_blast` | guard | `guard` → `deflect` | palm |
| push | `shockwave` | double palm burst | `f_palm_burst` | palm |
| sink | `smother_blast` | jump, palms down | `f_hop` | palm |
| tech | `fuse` | focus loop → point/snap | `c_fuse_loop` → `c_point` | sword |
| evade | `blast_jump` | blast jump | `f_hop` | fist |
| evade_hold | `afterglow` | hover by pops | `hover` | palm |

### 4.13 Air / Gust (Baguazhang)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `air_attack` Palm Gust→Cyclone→Gale→Hurricane | single palm; T1–T2 double palm change; T3 turning double palm | `a_palm` (T1/T2 `a_double_palm`, T3 `a_hurricane`) | oxtongue |
| thrust | `gust_crescent` | piercing palm | `a_pierce` | oxtongue |
| ground | `gust_dust_line` | swallow skims the water | `a_low_palm` | oxtongue |
| sweep | `gust_crosswind` | turning-body palm | `a_turn_palm` | oxtongue |
| guard | `guard` (Wind Guard) | circling palms | `a_guard` | oxtongue |
| push | `gust_wall` | big two-palm push | `a_wall_push` | palm |
| sink | `gust_downdraft` | palms press down | `a_downdraft` | palm |
| tech | `air_tech` Updraft / Wind Grip | spring up with rising palms / grip | `a_updraft` (grip: `a_pluck` → `a_guard` circling hold → `a_palm`, see `gust_grip` §4.17) | oxtongue |
| evade | `air_dash` | streamlined dash | `a_dash` | oxtongue |
| evade_hold | `gust_tailwind` | circle-walk run | `a_circle_walk` (fallback `run`) | oxtongue |

### 4.14 Air / Vortex (Bagua spins)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `vortex_twister` | spin on one foot | `a_spin` | oxtongue |
| thrust | `vortex_spiral` | spiralling piercing palm | `a_pierce` | oxtongue |
| ground | `vortex_funnel` | low palm sweep | `a_low_palm` | oxtongue |
| sweep | `vortex_eddy` | spin | `a_spin` | oxtongue |
| guard | `vortex_wall` | rising guard → circling palms | `a_rising_guard` → `a_guard` | oxtongue |
| push | `vortex_unleash` | two-palm push | `a_wall_push` | palm |
| sink | `vortex_funnel_down` | palms press down | `a_downdraft` | palm |
| tech | `vortex_eye` | wide gather overhead (steer while held) | `a_gather` (hold its last 24 frames) | oxtongue |
| evade | `vortex_spin_step` | spin step | `a_spin` (fast) | oxtongue |
| evade_hold | `vortex_whirl` | hover in a vortex | `hover` | oxtongue |

### 4.15 Air / Vacuum (Bagua plucking)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `vacuum_palm` Pressure Palm→Air Cannon→Implode→Collapse | palm; T1 double palm; T2–T3 turning drive | `a_palm` (T1 `a_double_palm`, T2+ `a_hurricane`) | palm |
| thrust | `vacuum_suction` | pluck (cai) | `a_pluck` | spread |
| ground | `vacuum_mine` | low palm | `a_low_palm` | palm |
| sweep | `vacuum_arc` | turning-body palm | `a_turn_palm` | oxtongue |
| guard | `vacuum_bubble` | circling palms | `a_guard` | oxtongue |
| push | `vacuum_wave` | two-palm push | `a_wall_push` | palm |
| sink | `vacuum_anchor` | palms press down, rooted | `a_downdraft` → `e_stone_skin` | palm |
| tech | `vacuum_well` | wide gather | `a_gather` | spread |
| evade | `vacuum_hop` | spring hop | `a_updraft` | oxtongue |
| evade_hold | `vacuum_slipstream` | circle-walk run | `a_circle_walk` | oxtongue |

### 4.16 Air / Sound (Bagua + percussive breath)
| slot | move | technique | clips | hands |
|---|---|---|---|---|
| strike | `sound_clap` Clap→Shout→Roar→Resonance | clap; T1+ chest-expanding shout | `a_clap` (T1+ `a_roar`) | palm |
| thrust | `sound_lance` | piercing palm | `a_pierce` | oxtongue |
| ground | `sound_tremor` | stomp | `f_stomp` | palm |
| sweep | `sound_echo_ring` | spin | `a_spin` | palm |
| guard | `sound_barrier` | circling palms | `a_guard` | palm |
| push | `sound_thunder_step` | boom dash | `a_dash` | palm |
| sink | `sound_ping` | stomp | `f_stomp` | palm |
| tech | `sound_flight` | spring up → flight | `a_updraft` → `flight` | oxtongue |
| evade | `sound_boom_step` | dash | `a_dash` | oxtongue |
| evade_hold | `sound_hover` | hover | `hover` | relaxed |

### 4.17 Chained actions (started by the sim mid-move, no slot of their own)
The sim switches the running action to these ids (`start_action` / `morph_action` in FourfoldCore), so they need map
entries of their own or the fighter drops to its stance. Each names clips of its own element only.

| slot | move | technique | clips | hands |
|---|---|---|---|---|
| chain | `lightning` (from `fire_attack` held ≥ `lightning_min`) | release through the stomach channel: two-finger extension (T3 sky strike) | `l_release` (T3 `l_skybreak`) | sword |
| chain | `pour` (from `fire_tech` on a molten body) | raise, press down, sweep the lava along the ground | `f_pour` | palm |
| chain | `vent` (legacy Vent) | double palms out of the horse, heat dumped | `f_palm_burst` | palm |
| chain | `gust_grip` (morph of `air_tech`) | pluck (cai), circle the gripped body, pushing-palm fling | `a_pluck` → `a_guard` → `a_palm` | oxtongue |
| chain | `flare_dash` (Fire/Flame dash, evade slot) | low sidestep on a flame jet | `evade_*` | fist |

---

## 5. Review checklist for every clip (the `animation` stream renders previews and LOOKS at them)
1. Silhouette reads from the gameplay camera (behind and above, 6–9 m) on a phone-sized render (640×296).
2. The contact frame is the most extended / fastest pose; no pose after contact goes further.
3. Planted feet do not slide (measure ankle and ball positions per frame; tolerance 3 mm).
4. No knee pops (thigh/calf near-straight snapping), no elbow hyperextension, no wrist candy-wrapping (twist bones).
5. Weight is over the support (pelvis projected inside the support polygon except in jumps and lunges).
6. Hands match the style's shape at contact; fingers do not intersect.
7. Loops are seamless (first = last frame, matching velocities across the seam).
8. Start and end poses equal the base stance pose within 1° per bone (short cross-fades).

## Sources
* Tai Chi 24-form postures: https://en.wikipedia.org/wiki/24-form_tai_chi · posture list in Chinese / English: https://qialance.com/?p=348
* Push-hands energies (peng, lü, ji, an; eight gates), silk reeling, fa jin: https://www.ymaa.com/articles/2021/01/training-contents-for-taiji-push-hands · https://thetaichinotebook.com/2023/08/14/what-is-the-point-of-silk-reeling/
* Tai Chi sword fingers (jian jue): https://modern-wushu.fandom.com/wiki/The_Big_Dipper_(Taijijian_Movement) · https://www.ymaa.com/publishing/book/tai-chi-sword-classical-yang-style-2nd-ed-complete-form-qigong-and-applications
* Hung Gar (stances, tiger claw, bridge hands, Taming the Tiger / Tiger-Crane / Iron Wire): https://en.wikipedia.org/wiki/Hung_Ga · https://practicalhungkyun.com/2015/09/applying-the-bridging-techniques-of-hung-ga/
* Northern Shaolin Long Fist and Tan Tui (extended kicks, whirlwind / butterfly / lotus kicks): https://en.wikipedia.org/wiki/Changquan · https://shaolin.org/video-clips-3/shaolin/tantui/overview.html
* Wushu stances (gong bu, ma bu, xu bu, pu bu, xie bu): https://en.wikipedia.org/wiki/Wushu_stances · https://www.flashmavi.com/wushu_stances
* Baguazhang (circle walking, kou bu / bai bu, single / double palm change, eight mother palms, spiral power): https://shaolin.org/general-2/kungfu-sets/baguazhang-circle-walking.html · https://neidan.discourse.group/t/the-eight-palm-changes-of-baguazhang/138
* Game-animation principles (anticipation, smears, startup / active / recovery, responsiveness): Mariel Cartwright, "Powerful and Effective Animation for Fighting Games" (GDC) https://gdcvault.com/play/1021657/Powerful-and-Effective-Animation-for · https://80.lv/articles/tips-on-making-fluid-and-powerful-animations
