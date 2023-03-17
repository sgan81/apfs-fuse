#pragma once

#include "Object.h"

class Spaceman : public Object
{
public:
	Spaceman();
	~Spaceman();

	int init(const void * params) override;

	uint64_t getFreeBlocks() const { return sm_phys->sm_dev[SD_MAIN].sm_free_count + sm_phys->sm_dev[SD_TIER2].sm_free_count; }

private:
	const spaceman_phys_t* sm_phys;
};
