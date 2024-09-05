#include <random>

#include <dmg/dmg.h>

static std::mt19937 rng(KOLY_SIGNATURE);

int reproRand() {
  return rng();
}
