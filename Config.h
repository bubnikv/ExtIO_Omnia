#pragma once

#define EXTIO_EXPORTS		1
#define HWNAME				"ExtIO_Omnia-0.1"
#define HWMODEL				"ExtIO_Omnia"
#define SETTINGS_IDENTIFIER	"ExtIO_Omnia-0.1"
// 5.3ms latency
#define EXT_BLOCKLEN		(512)			/* only multiples of 512 */
#define ZEROS_TO_MUTE		(32)
#define SAMPLE_RATE			(96000)
#define MUTE_ENVELOPE_LEN	(96*2)
#define CW_IQ_TONE_OFFSET	(1000)