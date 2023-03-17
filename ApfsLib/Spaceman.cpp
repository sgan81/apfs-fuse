#include "Spaceman.h"

Spaceman::Spaceman()
{
	sm_phys = nullptr;
}

Spaceman::~Spaceman()
{
}

int Spaceman::init(const void* params)
{
	(void)params;
	sm_phys = reinterpret_cast<const spaceman_phys_t*>(data());
	return 0;
}
