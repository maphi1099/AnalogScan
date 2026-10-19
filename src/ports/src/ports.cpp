// autoneg::ports besteht nur aus Schnittstellen. Diese Datei bindet jeden Header einzeln ein,
// damit der Build prüft, dass alle Header für sich übersetzbar sind.

#include "autoneg/ports/color.hpp"
#include "autoneg/ports/compute_backend.hpp"
#include "autoneg/ports/film_profile_source.hpp"
#include "autoneg/ports/image_io.hpp"
#include "autoneg/ports/inference.hpp"
#include "autoneg/ports/raw_decoder.hpp"
#include "autoneg/ports/repositories.hpp"
#include "autoneg/ports/services.hpp"
#include "autoneg/ports/task_executor.hpp"
#include "autoneg/ports/vision.hpp"
