// autoneg::ports besteht nur aus Schnittstellen. Diese Datei bindet jeden Header einzeln ein,
// damit der Build prüft, dass alle Header für sich übersetzbar sind.

#include "ports/color.hpp"
#include "ports/compute_backend.hpp"
#include "ports/film_profile_source.hpp"
#include "ports/image_io.hpp"
#include "ports/inference.hpp"
#include "ports/raw_decoder.hpp"
#include "ports/repositories.hpp"
#include "ports/services.hpp"
#include "ports/task_executor.hpp"
#include "ports/vision.hpp"
