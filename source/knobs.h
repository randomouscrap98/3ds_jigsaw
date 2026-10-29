#ifndef __HEADER_JIGSAW_KNOBS__
#define __HEADER_JIGSAW_KNOBS__

#include "edgegen.h"

#include <stdint.h>

// Knob (tab) shapes beyond edgegen's classic tab, as curves in edge space: t along the tab's
// span 0..1, n across it (times the tab's depth), from (0, 0) to (1, 0). cutgraph places them
// on edges; this only draws them. Used by the experimental wood cuts (not in the menu).

#define CUTKNOB_CLASSIC 0   // edgegen's tab
#define CUTKNOB_BALL 1      // Narrow neck, big round head
#define CUTKNOB_HOOK 2      // Head leaning to one side (earlet)
#define CUTKNOB_BLOCK 3     // Wide and squarish
#define CUTKNOB_FINGER 4    // Long and narrow
#define CUTKNOB_ROUND 5     // Like the classic tab but with a higher neck (thicker under the head)
#define CUTKNOB_CURL 6      // Thick spiral finger
#define CUTKNOB_SWIRL 7     // Double spiral: the line curls in and back out between its own
                            // turns, so both pieces get a finger wrapped round the other's
#define CUTKNOB_CIRCLE 8    // No neck: a circle sunk partway into the line (its opening is
                            // narrower than it, so it still locks), small fillets where it meets the line
#define CUTKNOBS 9
#define CUTKNOB_WOOD 6      // CutTabStyle knobs value for the wood knobs (BALL .. ROUND)

// Circle: radius KNOB_CIRCLE_R times the tab size (0.13 tab size: 0.3 of the span), centre
// KNOB_CIRCLE_SINK radii above the line, fillets reaching KNOB_CIRCLE_FILLET along it
#define KNOB_CIRCLE_R 2.3f
#define KNOB_CIRCLE_SINK 0.6f
#define KNOB_CIRCLE_FILLET 0.06f

// Curl: centerline rises straight for KNOB_CURL_STEM, then rolls over (like a wave crest),
// its curvature rising from KNOB_CURL_K0 to KNOB_CURL_K1 over KNOB_CURL_LEN; finger
// KNOB_CURL_WIDTH wide, flared at the base. Tab units, scaled by the tab's size jitter.
// Swept for the curliest (248°) that is thick enough at 0.1 cells on 0.7-1.0 cell edges.
#ifndef KNOB_CURL_STEM
#define KNOB_CURL_STEM 0.3f
#define KNOB_CURL_LEN 0.9f
#define KNOB_CURL_K0 2.6f
#define KNOB_CURL_K1 7.0f
#define KNOB_CURL_WIDTH 0.2f
#define KNOB_CURL_FLARE 0.8f        // Extra half widths at the base (fillet)
#endif
#define KNOB_CURL_SEG 18            // Centerline samples

// Swirl, in units of its span (cutgraph gives swirls a fixed span): arms KNOB_SWIRL_W apart
// (finger width), KNOB_SWIRL_TURNS turns each, centre lifted KNOB_SWIRL_LIFT finger widths,
// the arch ending KNOB_SWIRL_ARCH widths past the spiral
#ifndef KNOB_SWIRL_W
#define KNOB_SWIRL_W 0.13f
#define KNOB_SWIRL_TURNS 1.0f
#define KNOB_SWIRL_LIFT 1.0f
#define KNOB_SWIRL_ARCH 2.5f
#define KNOB_SWIRL_MARGIN 0.08f
#endif
#define KNOB_SWIRL_STEPS 14         // Samples per turn

#define KNOB_MAXPOINTS (4 * KNOB_CURL_SEG + 12)  // Most points one knob can have

// Sample a knob of kind (CUTKNOB_*; classic and unknown = edgegen_tabcurve) into out (at most
// KNOB_MAXPOINTS (t, n) pairs, flip applied). Returns the count.
int knob_curve(uint8_t kind, const EdgeTab * tab, float * out);

#endif
