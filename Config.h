#pragma once

#include <string>

#define EXTIO_EXPORTS		1
#define HWNAME				"ExtIO_Omnia-0.2"
#define HWMODEL				"ExtIO_Omnia"
#define SETTINGS_IDENTIFIER	"ExtIO_Omnia-0.2"
// 5.3ms latency
#define EXT_BLOCKLEN		(512)			/* only multiples of 512 */
#define ZEROS_TO_MUTE		(32)
#define SAMPLE_RATE			(96000)
#define MUTE_ENVELOPE_LEN	(96*2)
#define CW_IQ_TONE_OFFSET	(1000)

enum KeyerMode {
	KEYER_MODE_SK = 0,
	KEYER_MODE_IAMBIC_A = 1,
	KEYER_MODE_IAMBIC_B = 2,
};

struct Config
{
	// Serialize the config into a list of key=value pairs, separated by newlines.
	std::string serialize() const;
	// Deserialize the config from a list of key=value pairs, separated by newlines.
	void		deserialize(const char *str);
	// Validate the config data, set reasonable defaults if the values are out of bounds.
	void		validate();

	// TX IQ amplitude balance correction, 0.8 to 1.2;
	double		tx_iq_balance_amplitude_correction	= 1.;
	// TX IQ phase balance adjustment correcton, - 15 degrees to + 15 degrees.
	double		tx_iq_balance_phase_correction		= 0.;
	// TX power adjustment, from 0 to 1.
	double		tx_power							= 1.;

	KeyerMode	keyer_mode							= KEYER_MODE_IAMBIC_B;
	int			keyer_wpm							= 18;
};

extern Config g_config;
