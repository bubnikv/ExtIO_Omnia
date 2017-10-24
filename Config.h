#pragma once

#define EXTIO_EXPORTS		1
#define HWNAME				"Demo-1.00"
#define HWMODEL				"Demo ExtIO"
#define SETTINGS_IDENTIFIER	"Demo-1.x"
#define NUM_CARRIER         3
#define LO_MIN				100000LL
#define LO_MAX				7500000000LL
#define LO_PRECISION		5000L
// 15ms latency
#define EXT_BLOCKLEN		(1024)			/* only multiples of 512 */

#define SAMPLE_RATE			(96000)
