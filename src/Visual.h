#pragma once

#include "PCH.h"

// Efekt "latającej broni" w 1. osobie:
//  - ukrywa siatki rąk/rękawic (skinned geometry) – szkielet dalej animuje broń,
//  - podczas zamachu przesuwa i przechyla węzeł WEAPON w stronę ruchu myszy/drążka.
namespace Visual
{
	void Install();
}
