#pragma once

#include <QString>

// Shown names (9.8c; docs/spec/trademarks.md): the guides name some effects and effect parameter categories with Avid's
// marks (SubCap, PlasmaWipe, the Fluid effects, Illusion FX ...). Those names stay the stored keys (effects.json and
// effect_parameters.json, saved sequences, effect templates, AAF), and what the user sees is ours: shownName() turns a
// stored name into the one to show. Every place that shows an effect's or a parameter category's name goes through it.
// tests/data/trademarks.json lists the marks; scripts/check-trademarks.py reads the table in shown_names.cpp to check
// the shown names of the effects and categories.
namespace lumenis::model {

// The name to show for a stored effect, parameter category or parameter name: the marked words replaced by ours
// ("PlasmaWipe Avid Lava" is "Gradient Wipe Lava", "FluidBlur Effect Input Parameters" is "Flow Blur Effect Input
// Parameters"); any other name as it is.
QString shownName(const QString& stored);

} // namespace lumenis::model
