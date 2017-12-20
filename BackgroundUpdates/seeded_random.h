#pragma once

#include "random_xoroshiro128plus.h"

class seeded_random : public ExtraGenerators::xoroshiro128plus
{
public:
    seeded_random();
};

