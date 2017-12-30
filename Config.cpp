#include "Config.h"

#include <algorithm>

Config g_config;

std::string Config::serialize() const
{
	std::string out;
	char buf[1024];
	out += "extio_version=";
	out += SETTINGS_IDENTIFIER;
	out += "\n";
	sprintf(buf, "tx_iq_balance_amplitude_correction=%lf\n", this->tx_iq_balance_amplitude_correction);
	out += buf;
	sprintf(buf, "tx_iq_balance_phase_correction=%lf\n", this->tx_iq_balance_phase_correction);
	out += buf;
	sprintf(buf, "tx_power=%lf\n", this->tx_power);
	out += buf;
	out += "keyer_mode=";
	switch (this->keyer_mode) {
	case KEYER_MODE_SK:			out += "SK\n";		 break;
	case KEYER_MODE_IAMBIC_A:	out += "IAMBIC_A\n"; break;
	case KEYER_MODE_IAMBIC_B:	out += "IAMBIC_B\n"; break;
	}
	sprintf(buf, "keyer_wpm=%d\n", this->keyer_wpm);
	out += buf;
	return out;
}

void Config::deserialize(const char *str)
{
	const char *p = str;
	while (*p != 0) {
		const char *endptr = strchr(p, '\n');
		if (endptr == nullptr)
			endptr = p + strlen(p);
		const char *eqptr = strchr(p, '=');
		if (eqptr != nullptr && eqptr < endptr) {
			std::string key(p, eqptr);
			std::string value(eqptr + 1, endptr);
			if (key == "tx_iq_balance_amplitude_correction")
				this->tx_iq_balance_amplitude_correction = atof(value.c_str());
			else if (key == "tx_iq_balance_phase_correction")
				this->tx_iq_balance_phase_correction = atof(value.c_str());
			else if (key == "tx_power")
				this->tx_power = atof(value.c_str());
			else if (key == "keyer_mode") {
				if (value == "SK")
					this->keyer_mode = KEYER_MODE_SK;
				else if (value == "IAMBIC_A")
					this->keyer_mode = KEYER_MODE_IAMBIC_A;
				else if (value == "IAMBIC_B")
					this->keyer_mode = KEYER_MODE_IAMBIC_B;
			} else if (key == "keyer_wpm")
				this->keyer_wpm = int(atoi(value.c_str()));
		}
		p = endptr;
		while (*p == '\n')
			++ p;
	}
}

void Config::validate()
{
	tx_iq_balance_amplitude_correction = std::min(std::max(tx_iq_balance_amplitude_correction,   0.8),  1.2);
	tx_iq_balance_phase_correction     = std::min(std::max(tx_iq_balance_phase_correction,     -15. ), 15.);
	tx_power						   = std::min(std::max(tx_power,						     0. ),  1.);
	keyer_wpm						   = std::min(std::max(keyer_wpm,							 5  ), 45 );
}
