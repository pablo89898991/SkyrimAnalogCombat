#pragma once

#include "PCH.h"

// Efekt "latającej broni" w 1. osobie:
//  - ukrywa siatki rąk/rękawic (skinned geometry),
//  - w trybie proceduralnym sam ustawia broń względem kamery: spoczynek, zamach
//    po okręgu i cięcie łukiem przez środek ekranu pod dowolnym kątem.
namespace Visual
{
	void Install();
	void RegisterEvents();  // po kDataLoaded

	// Czy w tej chwili broń w danej ręce animuje mod (1. osoba, broń biała, [Procedural] Enabled).
	bool ProceduralActive(bool a_left);
}
