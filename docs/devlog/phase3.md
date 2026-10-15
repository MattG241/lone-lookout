# Phase 3 — World building

**Goal (DESIGN.md §13):** worldgen bakers, chunked terrain, foliage with LOD and wind, the tarn,
all six areas dressed, the tower and cabin interiors, the interactables, days, weather and
season, and the notebook. Every area must be reachable on foot.

Every screenshot below is a **real Dolphin frame dump** of `lonelookout_shots.dol`, captured at
the Wii's native 640 × 480 (Dolphin 2609, Vulkan backend, copy filter on, forced 4:3). Run
`tools/capture/capture_shots.sh` to reproduce them.

## What was built

| Area | Delivered |
|---|---|
| World data | `tools/worldgen/bake_world.py` bakes the Phase 1 map (`ashen_peaks.py`) into the `.dol`. It writes 129² heights at 4 m and late-summer and early-autumn vertex colours painted from the worldgen masks: grass, needle duff, meadow, ash, fireweed, scree, cinder, lakebed, sedge, creek margins and trail wear. It also writes a ground-type grid, ~1,860 Poisson-spaced trees and ~6,600 tufts, `world_data.h` (places, trails, the creek, footbridge positions) and the notebook map, lettered in Lookout Hand. |
| Terrain grading | The Phase 1 sketch had 70° trail sections, so the knob and ridge were lowered and widened. The ridge is now asymmetric: long and walkable toward the bowl. Every trail is graded into the hillside, with a smoothed, grade-limited bed profile that the ground is cut or filled to. Junctions agree because each trail is pinned to the bed of the one it starts from. |
| Renderer | 8 × 8 chunked terrain with edge skirts. Seasonal recolouring re-records the lists behind the day card. Trail and creek ribbons are draped on the exact mesh. The tarn has per-frame Fresnel colour and a sun glint. Near trees sway individually; far chunks are one pre-transformed list each. Tufts come in seven kinds: grass, larkspur, ferns, sedge, fireweed, saplings and rocks. A soft cloud layer, rain streaks, lightning, and an interior material lit by the lamp. |
| Kestrel Lookout | Four stair flights wind round the tower, with corner landings, handrails and support posts. Catwalk, the glazed cab (glass invisible from inside), a pyramid roof, the lightning rod and aerial. Inside: the fire finder, cot (with Augie's map under it), stove and kettle, the radio table with handset, logbook and lamp, Augie's shelf, the bird list, the lightning stool on glass feet, and binoculars on the hook. A woodpile and cistern at the base. |
| The other areas | **Pell's Cabin:** board walls, west window, open door, gable roof, porch, spoon chimes, rain barrel. Inside: a table with a drawer and the pressed-flower book, a chair, a bunk and a shelf. **Tarn:** the dock and the tin box. **Ridge:** Augie's carved bench and the cairn. **Meadow:** the half-fallen drift fence and a log. **Burn:** the tagged snag and a fallen snag. **Cold spring:** pipe, trough and cup. **Trailhead:** supply box, sign, the road gate and its fence. Footbridges wherever a trail crosses the creek. |
| Movement | Walk patches for the deck, the flights, the landings, the cabin and the dock. General rules stand in for most railings: never walk off a drop of more than 1 m, never walk into a structure at body height, never enter a slope over 38°. Walls with door gaps, posts, boxes and trunks push the player out. The district line is the rim, the deep tarn and the gate, each with a one-time thought. |
| Interactables | 11 placed notes (N01–N11; N12 arrives in the Day 6 supply box) and 7 sit spots (stool, catwalk, meadow log, dock, ridge bench, fallen snag, porch step). Also the cot (sleep from 19:00), lamp, kettle, logbook, fire finder, radio handset, binoculars, supply box, chimes and gate. The third stair from the top sings a slightly flat E-flat. |
| Days and weather | Seven days with dates and season. Day 1 starts at 15:30 at the trailhead; the radio check rings on first entering the cab (or by 17:00). Sleeping moves to the next morning, recolours the land and shows the day card. Weather: clear, warm, high cloud, a dry storm on Day 4 (14:00–17:30, with lightning and delayed thunder), orange haze on Day 5, crisp air on Day 6, and rain from noon on Day 7. |
| Tools and UI | The **notebook** (D-pad ↓) has Map (with a pencil mark where you stand), Notes, Radio log and Sounds pages. The **binoculars** (D-pad ↑) zoom to 16° with a twin-circle view and pencil labels on places. The **fire finder** gives a sighting ring with the six areas' bearings. Place names appear on arrival, Theo has passing thoughts, and there are small control hints. |
| Audio | New footsteps on needles, ash and scree. The spoon chimes, the singing stair, the kettle whistle, thunder and the lamp switch. A rain bed with drips (brighter on a roof), and wind muffled indoors. |
