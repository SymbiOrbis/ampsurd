# AMPSURD - UPDATED UI/UX SPECIFICATION
This document supersedes the previous UI concepts for the project formerly working under the names MONSTROSITY and QUINQUEPLEX.
The new working/product name is:
AMPSURD
Please preserve all currently working DSP, NAM loading, routing and other functionality unless a change below explicitly requires a modification. This is primarily a UI/UX specification, not a request to rewrite working DSP.
CORE PRODUCT CONCEPT
AMPSURD is a freeware/open-source guitar plugin whose defining function is mixing up to five Neural Amp Modeler captures in parallel.
The same guitar DI signal is processed through up to five NAM models and their outputs are blended into one composite guitar sound.
The interface should communicate this concept immediately.
The design must NOT look specifically like a metal-only product even though high-gain/metal users are an important target group.
Do not use photographs or illustrations of real or fictional amplifiers in the five NAM slots.
GENERAL VISUAL DIRECTION
The latest preferred direction is:
- dark
- modern
- clean
- minimalist
- flat or near-flat design
- neutral dark grey background
- no background gradients
- no photorealistic amp imagery
- no skeuomorphic amp controls
- no vintage or retro rack aesthetic
- no "Winamp" aesthetic
- no excessive decoration
- no distressed, industrial or baroque styling
- very restrained use of colour
- thin, precise lines
- strong spacing and alignment
- high legibility
- professional modern audio-software appearance
The design should feel contemporary rather than nostalgic.
White or light grey on dark grey should dominate.
Accent colour should be used extremely sparingly, if at all. The interface must not depend on colour for understanding its state.
The EQ should specifically NOT use different colours for individual bands. Use neutral white or light-grey nodes and curves.
MAIN LAYOUT
The plugin should have five principal horizontal areas:
1. Header
2. Five fixed NAM slots
3. Expandable editing area
4. Master input/output area
5. Thin branding footer
The five NAM slots are FIXED.
Do NOT dynamically resize or rearrange the interface depending on how many NAM models are loaded.
There are always five equal slots visible.
This is intentional: the five positions are part of the visual identity and core concept of AMPSURD.
HEADER
Left:
AMPSURD
No "5 times 1 equals 666" formula in the normal interface.
That joke may potentially appear later as an Easter egg or About-page element, but it must not be part of the primary UI.
Right:
PRESET, with preset name and dropdown.
SAVE.
SAVE AS.
Settings icon if required.
PRESETS
There is ONE preset system.
A preset represents the complete AMPSURD rig.
It should store all relevant state, including:
- loaded NAM files for slots 1 through 5
- mix ratios
- active and mute states
- EQ settings
- alignment and phase settings
- master settings
- any other sound-relevant plugin state
Do NOT implement separate per-amp presets or EQ presets.
That would unnecessarily complicate the workflow.
FIVE NAM SLOTS
Five equal approximately square or rectangular modules are permanently displayed next to each other.
Each contains:
- NAM filename display
- LOAD NAM
- EDIT
- SOLO
- MUTE
- vertical mix fader
- numerical mix percentage
Do not display fake amplifier artwork.
Do not attempt to infer the physical amplifier represented by a NAM.
NAM NAME DISPLAY
This is an important part of the visual identity.
The filename itself should become the large central visual element of each slot.
Example actual filename:
Rectifier underscore CH3 underscore Modern underscore SM57 underscore A2 dot NAM.
Display:
Rectifier underscore CH3 underscore Modern underscore SM57 underscore A2.
The dot NAM extension may be omitted from the large display.
IMPORTANT TYPOGRAPHY RULES
All five slots MUST use EXACTLY THE SAME font size for their NAM names.
Do NOT make short names larger.
For example:
5153
must use exactly the same character size as:
Rectifier underscore CH3 underscore Modern underscore SM57 underscore A2.
This is a deliberate design decision.
Do not auto-scale the font based on filename length.
Do not make "5153" fill the available space.
The chosen fixed font size should be relatively large - approximately the size used for the longer capture names in the latest mock-up.
TEXT WRAPPING
Long filenames may naturally continue over multiple centered lines.
Target approximately one to four lines depending on filename length and available width.
Do not semantically reinterpret, beautify or rename the filename.
Preserve:
- underscores
- capitalization
- numbers
- original spelling
Do not insert hyphens or modify words.
The text block should remain horizontally centered and, as a complete block, visually centered within the available filename area.
For example, Rectifier underscore CH3 underscore Modern underscore SM57 underscore A2 might render across approximately three lines depending on actual line-breaking and layout constraints.
The precise wrapping algorithm can be determined technically, but the principle is:
- fixed font size
- centered text
- multiple lines allowed
- preserve original characters
- no semantic rewriting
- no font scaling
If an exceptionally long filename still cannot fit within the maximum available lines, simply clip or truncate the remaining content.
The complete actual filename should remain accessible through an appropriate tooltip, dropdown or file-information mechanism.
Do NOT add a user-defined short alias or name system at this stage.
The actual NAM filename is the identity shown by AMPSURD.
NAM COMPATIBILITY
Loading should continue to treat normal third-party dot NAM files as first-class citizens.
Priority:
- NAM A2 Full
- NAM A2 Lite
- legacy NAM formats wherever supported by the current NeuralAmpModelerCore
No proprietary conversion or lock-in.
MIX FADERS
Each slot has a conventional vertical mixer-style fader on its right side.
This is not intended to behave simply as five unrelated volume controls.
The faders represent the relative contribution of each active NAM model to the combined sound.
The UI should show intuitive percentages.
For example:
Amp 1: 42 percent.
Amp 2: 31 percent.
Amp 3: 18 percent.
Amp 4: 7 percent.
Amp 5: 2 percent.
The important interaction concept is this:
When one mix fader is increased, the contribution of the other active paths should decrease so that the overall mix remains normalized rather than simply adding gain.
The objective is:
"I want more of this amp in the combined sound."
Rather than:
"I want to add X decibels and then compensate master volume."
The exact DSP and mixing law should be designed carefully.
Do NOT assume that simply forcing a mathematical sum of linear gains to 100 percent will necessarily preserve perceived loudness.
The implementation should avoid:
- major loudness jumps
- unexpected clipping
- severe level changes when moving between one and several models
Please treat this as a DSP design requirement and test it experimentally.
The UI can nevertheless present the result as simple intuitive percentages.
SOLO AND MUTE
Each NAM slot has:
SOLO.
MUTE.
These should behave as expected in a mixer.
Interactions between solo, mute and normalized mix ratios should be predictable and should not destroy the user's stored underlying mix proportions.
EDIT
Each slot contains EDIT.
The philosophy is:
The NAM blend itself is the primary sound-design mechanism.
EQ is SECONDARY.
A user who never opens EDIT should still experience the complete core concept of AMPSURD.
Clicking EDIT selects that NAM path and opens or activates the lower editing section.
The currently edited slot should receive a subtle visual indication.
PARAMETRIC EQ
Each NAM path should have its own 10-band parametric EQ.
The EQ is primarily a corrective and blending tool, for example to remove frequency conflicts between captures.
It should not visually dominate the plugin.
Use a large graphical EQ area.
For each band support:
- frequency
- gain
- Q
Prefer direct manipulation of nodes in the graph.
Possible interaction:
Horizontal drag equals frequency.
Vertical drag equals gain.
Mouse wheel or secondary gesture equals Q.
Double click equals reset or flat.
Exact gestures can be adjusted for usability.
IMPORTANT:
All EQ nodes and curves should use neutral monochrome styling.
NO rainbow or multicoloured EQ bands.
Use white or light grey on dark grey.
ALIGNMENT AND PHASE
The editing area should also contain the phase and alignment controls already planned for AMPSURD.
Default:
AUTO.
AMPSURD should automatically compensate relevant timing and phase differences between parallel NAM paths where technically possible.
Also retain the planned experimental or manual mode that lets the user alter the relative phase or time relationship creatively.
The technical implementation must distinguish real phase manipulation from simple coarse delay.
A fractional-sample delay or appropriate phase-manipulation approach should be evaluated rather than presenting technically misleading controls.
MASTER AREA
Below the edit section:
INPUT.
Input level and meter.
OUTPUT.
Output level and meter.
BYPASS.
Keep this area minimal.
BRANDING FOOTER
Add a permanent thin footer at the very bottom.
It is part of the normal UI, not an external banner.
Layout:
BROUGHT TO YOU BY.
Then three equal rectangular logo areas.
The three logos are:
1. THE BLACK DEATH ENSEMBLE.
2. DARK MATTER.
3. SYMBIORBIS.
Reserve THREE EQUAL RECTANGULAR LOGO AREAS.
The logos should have equal visual status and approximately equal bounding boxes.
"Brought to you by" sits immediately to their left.
The footer should:
- use the same dark-grey UI background
- look integrated into the plugin
- remain visually subordinate to AMPSURD
- preferably use monochrome or light-grey logo versions
- not look like an advertising banner
- consume very little vertical space
If final logo assets are not yet available, create clean equal placeholder containers and make replacing them with actual assets trivial.
Do NOT use arbitrary AI approximations of the real logos in the production version.
DESIGN REFERENCE AND CURRENT PREFERRED MOCK-UP
The most recent visual direction established in the discussion is:
- flat dark-grey background
- five outlined modules
- thin grey borders
- very little colour
- monochrome EQ
- large filenames
- vertical faders
- integrated footer
The important correction to the current visual mock-up is:
ALL FIVE NAM FILENAMES MUST USE THE SAME FONT SIZE.
In particular, a short name such as "5153" must NOT be enlarged compared with a long filename.
Use the same relatively large font size everywhere and let longer filenames wrap over multiple centered lines.
IMPLEMENTATION PRIORITIES
Please do not sacrifice current working audio functionality merely to achieve the visual design.
Recommended order:
1. Preserve and verify current DSP functionality.
2. Establish the five fixed-slot layout.
3. Implement consistent filename typography.
4. Implement load controls and existing functionality.
5. Implement linked and normalized mix faders correctly.
6. Implement EDIT selection.
7. Implement 10-band per-path EQ.
8. Integrate alignment and phase controls.
9. Implement complete-rig preset save and restore.
10. Add master input and output.
11. Add branding footer and final visual polish.
REAL-TIME SAFETY
Continue following the existing architecture requirements:
- no filesystem operations on the audio thread
- no model parsing or loading on the audio thread
- no memory allocation in real-time processing where avoidable
- no blocking or network activity on the audio thread
- safely load models off-thread and swap them into processing
- avoid clicks and pops when loading or switching models
- preserve thread safety
DOCUMENTATION
Please update PROJECT_STATUS.md after implementing or planning these changes.
Record:
- AMPSURD as the current product name
- this UI specification
- what has actually been implemented
- what remains mock-up or planned
- files changed
- build and test status
- known issues
- next concrete development step
Do not claim visual or DSP functionality as complete until tested in the actual plugin.
Most importantly:
KEEP THE PRODUCT SIMPLE.
AMPSURD is fundamentally:
LOAD UP TO FIVE NAMs.
BLEND THEM.
OPTIONAL CORRECTIVE EDITING.
PLAY.
The sophisticated DSP should exist underneath a very obvious interface.