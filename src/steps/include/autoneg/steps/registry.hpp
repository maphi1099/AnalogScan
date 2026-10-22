#pragma once

#include "autoneg/base/error.hpp"
#include "autoneg/pipeline/step_registry.hpp"
#include "autoneg/steps/other_steps.hpp"

namespace autoneg::steps {

/// Registriert alle Verarbeitungsschritte von Version 1.0 (Architektur 5.2). Die
/// Ausgabetransformation (A3) ist keine registrierte Stufe, sondern Teil des Ausführungsprofils
/// (pipeline::OutputPass). Aufruf in der Kompositionswurzel.
base::Expected<void> registerStandardSteps(pipeline::StepRegistry& registry, FlatFieldCache& flatFields);

} // namespace autoneg::steps
