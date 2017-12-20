#include "stdafx.h"

#include "seeded_random.h"

seeded_random::seeded_random()
{
    std::random_device rd;

    std::seed_seq seq{ rd(), rd(), rd(), rd() };

    seed(seq);
}

