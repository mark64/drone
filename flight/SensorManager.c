// implementation of the sensor manager header
//
// this is one of the few files that may have to be rewritten for each new system
// this imeplementation is for the cleanDrone
//
// configuration values taken from the data sheet located at the following URLs
//
// accelerometer and gyroscope
// http://www.st.com/content/ccc/resource/technical/document/datasheet/a3/f5/4f/ae/8e/44/41/d7/DM00133076.pdf/files/DM00133076.pdf/jcr:content/translations/en.DM00133076.pdf
// 
// magnetometer
// http://www.nxp.com/assets/documents/data/en/data-sheets/MAG3110.pdf
//
// barometer
// https://cdn-shop.adafruit.com/datasheets/BST-BMP180-DS000-09.pdf
//
// by Mark Hill
#include<stdio.h>
#include<unistd.h>
#include<math.h>

#include "SensorManager.h"
#include "i2cctl.h"


static const uint8_t accelAddress = 0x6b;
static const uint8_t gyroAddress = 0x6b;
static const uint8_t magAddress = 0x0e;
static const uint8_t barometerAddress= 0x77;

// the barometer puts all the calculation responsibility on the user,
//   so you have to retrieve and store the calibration values to calculate the
//   pressure and temperature
static int32_t barometerCalibrationValues[11];
// internal function used to retrieve barometer values
void getBarometerParameters();

// this value is 0 when sensors have not been initialzed and is set to
//   1 by the initialize sensors function, indicating sensors are configured
static uint8_t _sensorsAvailable = 0;

// converts signed value into two's complement form
uint32_t unsignedValue(int _signedValue, uint8_t numBits);
// converts two's complement unsigned value into signed value
int32_t signedValue(int _unsignedValue, uint8_t numBits);

void initializeSensors() {
	// no need to run if sensors already initialized
	if (_sensorsAvailable == 1) {
		return;
	}
	
	//
	// accelerometer section
	//
		
	// enabled auto increment on the register addresses
	uint8_t autoIncrementRegister[1] = {0x12};
	uint8_t autoIncrement = 0x04;
	int incrementSuccess = i2cWrite(accelAddress, autoIncrementRegister, 1, autoIncrement, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);
	
	if (incrementSuccess != 0) {
		printf("Failed to set auto increment for accelerometer\n");
	}
	
	uint8_t accelConfigRegisters[1] = {0x10};
	// sets the accelerometer to 1.6kHz mode with a range of +-16g
	uint8_t accelConfig = 0x84;
	int accelSuccess = i2cWrite(accelAddress, accelConfigRegisters, 1, accelConfig, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);
	
	if (accelSuccess != 0) {
		printf("Failed to set i2c configuration for accelerometer\n");
	}

	//
	// gyroscope section
	//
	uint8_t gyroConfigRegister[1] = {0x11};
	// sets the gyroscope to 1.6kHz mode with a scale of 2000 degrees per second
	uint8_t gyroConfig = 0x84;
	int gyroSuccess = i2cWrite(gyroAddress, gyroConfigRegister, 1, gyroConfig, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);

	// sets the gyroscope high pass filter on and sets the filter cutoff frequency
	//   to 2.07Hz with rounding enabled
	gyroConfigRegister[0] = 0x16;
	gyroConfig = 0x70;
	gyroSuccess |= i2cWrite(gyroAddress, gyroConfigRegister, 1, gyroConfig, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);

	if (gyroSuccess != 0) {
		printf("Failed to set i2c configuration for gyroscope\n");
	}
	
	//
	// magnetometer section
	//
	
	// annoyingly, the device must be put to sleep when changing settings
	// first, set the device to sleep
	uint8_t magConfigRegister1 = 0x10;
	uint8_t magSleepValue = 0x00;

	int magSuccess = i2cWrite(magAddress, &magConfigRegister1, 1, magSleepValue, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);

	// set the user offset values (experimentally determined, different for every setup
	uint8_t xOffsetRegisters[2] = {0x09, 0x0a};
	uint8_t yOffsetRegisters[2] = {0x0b, 0x0c};
	uint8_t zOffsetRegisters[2] = {0x0d, 0x0e};
	
	// bits must be shifted by one because the last bit is 0 and unused
	uint16_t xOffset = (0x7f4a << 1);
	uint16_t yOffset = (0x0241 << 1);
	uint16_t zOffset = (0x0604 << 1);
	
	// this block was used to determine the offset values based on experimental testing
	//int value = -183;
	//printf("two complement of %d is %x\n", value, unsignedValue16bit(value));	

	magSuccess |= i2cWrite(magAddress, xOffsetRegisters, 2, xOffset, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);
	magSuccess |= i2cWrite(magAddress, yOffsetRegisters, 2, yOffset, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);
	magSuccess |= i2cWrite(magAddress, zOffsetRegisters, 2, zOffset, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);

	// now set the register values
	uint8_t magConfigRegisters[2] = {0x10, 0x11};
	uint16_t magConfig = 0x0900;

	magSuccess |= i2cWrite(magAddress, magConfigRegisters, 2, magConfig, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);

	// finally, wake up the magnetometer again
	uint8_t magWakeValue = 0x09;

	magSuccess |= i2cWrite(magAddress, &magConfigRegister1, 1, magWakeValue, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);

	if (magSuccess != 0) {
		printf("Failed to set i2c configuration for magnetometer\n");
	}
	
	//
	// barometer section
	//
	
	getBarometerParameters();

	// let power stabilize with a wait
	usleep(50);
	_sensorsAvailable = 1;
}


// set sensors to sleep mode
void deinitializeSensors() {
	//
	// accelerometer and gyroscope section
	//

	// expanded for readability, local variables for the configRegisters
	uint8_t accelConfigRegister = 0x10;
	uint8_t gyroConfigRegister = 0x11;

	uint8_t registers[] = {accelConfigRegister, gyroConfigRegister};

	// since I want to turn the sensors off, I can just write straight zeros
	uint16_t writeValue = 0x0000;

	// perform the actual write and check for errors
	int success = i2cWrite(accelAddress, registers, 2, writeValue, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);

	if (success != 0) {
		printf("Failed to deinitialize accelerometer and gyroscope sensors\n");
	}

	//
	// magnetometer section
	//
	uint8_t magConfigRegisters[2] = {0x11, 0x12};
	uint16_t magConfig = 0x0800;

	int magSuccess = i2cWrite(magAddress, magConfigRegisters, 2, magConfig, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);

	if (magSuccess != 0) {
		printf("Failed to power down magnetometer\n");
	}

	//
	// barometer section
	//
	// sets the barometer to oversampling @ 8 times
	uint8_t barometerConfigRegister = 0xf4;
	uint8_t barometerConfig = 0x00;

	int barometerSuccess = i2cWrite(barometerAddress, &barometerConfigRegister, 1, barometerConfig, HIGH_BYTE_FIRST, AUTO_INCREMENT_DISABLED);

	if (barometerSuccess != 0) {
		printf("Failed to power down barometer\n");
	}
	
}


// values are usually returned in two's complement
// this returns the signed value from the raw two's complement input
int32_t signedValue(uint32_t _unsignedValue, uint8_t numBits) {
	// maximum positive value used for comparison
	uint32_t maxPos = pow(2, numBits - 1) - 1;
	// maximum value the number can take based on the number of bits
	// in this case, I am using 16 bit values
	uint32_t maxNeg = 2 * (maxPos + 1);
	
	// the value to be returned, assuming it is positive
	int result = _unsignedValue;
	if (_unsignedValue > maxPos) {
		result = -1 * (int32_t)(maxNeg - result);
	}

	return result;
}


// converts from a signed value into a two's complement value
uint32_t unsignedValue(int _signedValue, uint8_t numBits) {
	// maximum value the number can take based on the number of bits
	// in this case, I am using 16 bit values
	uint32_t maxNeg = pow(2, numBits);
	
	uint32_t result = _signedValue;

	if (_signedValue < 0) {
		result = maxNeg - (-1 * _signedValue) + 1;
	}

	return result;
}

// this is a generalized function used by the acceleration vector,
//   rotation vector, and magnetometer vector functions since they have
//   similar hardware interfaces
// place registers in x, y, z order with low before high registers
// with the exception of the last argument, all arguments are for the i2c operation
//   see the documentation in i2cctl.h for information on what the values do
//   it only applies for 16 bit values right nowm set to 0 if using 32 bit or 8 bit sizes
// the last argument, divisor, is used to correct for the fact that decimal values
//   must be stored as integers by the registers, so the decimal point must be shifted
struct Vec3double threeAxisVector(uint16_t address, uint8_t *registers, uint8_t numRegisters, uint8_t bytesPerValue, uint8_t autoIncrementEnabled, uint8_t highByteFirst, double divisor) {
	if (numRegisters / bytesPerValue > 3 || numRegisters / bytesPerValue <= 0) {
		printf("Invalid register count for function threeAxisVector\n");
		return vectorFromComponents(0, 0, 0);
	}
	
	// retrieves the sensor values based on the passed in arguments
	uint32_t vectorValues[numRegisters / bytesPerValue];
	int success = i2cWordRead(address, registers, numRegisters, vectorValues, bytesPerValue, highByteFirst, autoIncrementEnabled);

	// check for failure and return an empty vector if so
	if (success != 0) {
		printf("Read failed in threeAxisVector for device %x", address);
		return vectorFromComponents(0, 0, 0);
	}

	// convert the values from unsigned two's complement to a signed int
	int rawx = signedValue(vectorValues[0], 16);
	int rawy = signedValue(vectorValues[1], 16);
	int rawz = signedValue(vectorValues[2], 16);

	double x = rawx / divisor;
	double y = rawy / divisor;
	double z = rawz / divisor;

	return vectorFromComponents(x, y, z);
}

// gets the linear acceleration from the gyroscope
struct Vec3double accelerationVector() {	
	// initialize the sensors before using them
	initializeSensors();
	
	// the hard-coded register addresses for the accelerometer (L,H,L,H,L,H) (x,y,z)
	uint8_t accelRegisters[6] = {0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d};
	
	// convert the signed integer form into double form based on the configured accelerometer range
	// scale is given as the +- g range for the accelerometer
	// with a +- 16g scale and a 16 bit output integer, the raw value should be 
	//   divided by 2^(bits - log2(range)) which comes out to be 2^(15 - log2(16))
	//   which equals 2^11 = 2048
	// why 15? well remember, the raw 16 bit register output has to be converted to a signed value
	//   two's complement effectively uses a bit to encode sign, so we lost a bit for the total amount
	// for the sake of efficiency, this value is hardcoded, but it is important to know how it was 
	//   determined for future adaptation
	int divisor = 2048;

	// create an even more user-friendly acceleration vector
	struct Vec3double acceleration = threeAxisVector(accelAddress, accelRegisters, 6, WORD_16_BIT, AUTO_INCREMENT_ENABLED, LOW_BYTE_FIRST, divisor);

	return acceleration;
}



// returns the vector containing the angular rotation rate of the gyroscope
// also if you were wondering, yes I did just copy paste the acceleration function
//   and then change variable names and comments.  It's necessary boilerplate, 
//   the sensors are on the same chip, and I don't care if you know
struct Vec3double rotationVector() {	
	// initialize the sensors before using them
	initializeSensors();
	
	// the hard-coded register addresses for the gyroscope (L,H,L,H,L,H) (x,y,z)
	uint8_t gyroRegisters[6] = {0x22, 0x23, 0x24, 0x25, 0x26, 0x27};
	
	// scale is given as the +- dps range for the gyroscope
	// with a +- 2000 dps scale and a 16 bit output integer, the raw value should be 
	//   divided by 2^(bits - log2(range)) which comes out to be 2^(15 - ceil(log2(2000)))
	//   which equals 2^4 = 16
	// 
	// why 15? well remember, the raw 16 bit register output has to be converted to a signed value
	//   two's complement effectively uses a bit to encode sign, so we lost a bit for the total amount
	// for the sake of efficiency, this value is hardcoded, but it is important to know how it was 
	//   determined for future adaptation
	//
	// looking back, I have no idea how this divisor became 32 when the math says 16
	// dont ask me
	int divisor = 14;

	// create an even more user-friendly rotation vector
	struct Vec3double rotation = threeAxisVector(gyroAddress, gyroRegisters, 6, WORD_16_BIT, AUTO_INCREMENT_ENABLED, LOW_BYTE_FIRST, divisor);

	return rotation;
}


// returns the vector describing the magnetic field
// vector axises (no idea how to make axis plural) are the same as the accelerometer axises
struct Vec3double magneticField() {
	// initialize the sensors before using them
	initializeSensors();

	// the hard-coded register addresses for the magnetometer (L,H,L,H,L,H) (x,y,z)
	uint8_t magRegisters[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
	
	// scale is given as the +- micro telsas (uT) range for the magnetometer
	// with a +- 1000uT scale and a 16 bit output integer, the raw value should be 
	//   divided by 2^(bits - log2(range)) which comes out to be 2^(15 - ceil(log2(1000)))
	//   which equals 2^5 = 32
	// however, experimentally, that gives the wrong value, but the number below does work
	//   dont ask me why, it just works
	// why 15? well remember, the raw 16 bit register output has to be converted to a signed value
	//   two's complement effectively uses a bit to encode sign, so we lost a bit for the total amount
	// for the sake of efficiency, this value is hardcoded, but it is important to know how it was 
	//   determined for future adaptation
	int divisor = 10;

	// create an even more user-friendly magnetic field vector
	struct Vec3double magneticField = threeAxisVector(magAddress, magRegisters, 6, WORD_16_BIT, AUTO_INCREMENT_ENABLED, HIGH_BYTE_FIRST, divisor);
	
	// since the magnetometer is actually mounted upside down, the z axis value must be flipped
	magneticField.z *= -1;

	return magneticField;
}

// retrieves the default parameters for the barometer as defined in the eeprom registers and
//   stores them in the barometerCalibrationValues array in the order they appear on the data sheet
void getBarometerParameters() {
	// loop through the 11 values and get the register contents
	// convert to signed values if needed
	for (uint8_t i = 0; i < 11; i++) {
		// the registers come in pairs of two
		// they are MSB (most significant byte) first
		uint8_t registers[2] = {(uint8_t)(0xaa + (2 * i)), (uint8_t)(0xab + (2 * i))};
		uint32_t readValue;

		int success = i2cWordRead(barometerAddress, registers, 2, &readValue, WORD_16_BIT, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);

		if (success != 0) {
			printf("reading eeprom data from registers %x and %x failed\n", registers[0], registers[1]);
		}
		else {
			int32_t signedReadValue;

			if (i < 3 || i > 5) {
				signedReadValue = signedValue(readValue, 16);
			}
			else {
				signedReadValue = readValue;
			}

			barometerCalibrationValues[i] = signedReadValue;
		}
	}
}


// helper function to get the barometer uncompensatedtemperature
uint32_t uncompensatedTemperature() {
	uint8_t barometerRegister = 0xf4;
	int success = i2cWrite(barometerAddress, &barometerRegister, 1, 0x2e, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);
	
	// datasheet suggests waiting 4.5 seconds for the value to be obtained
	usleep(4500);

	uint8_t dataRegisters[] = {0xf6, 0xf7};
	uint32_t temperature;
	success |= i2cWordRead(barometerAddress, dataRegisters, 2, &temperature, WORD_16_BIT, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);

	if (success != 0) {
		printf("barometer temperature read failed");
	}

	return temperature;
}

// helper function to get the barometer uncompensated pressure
uint32_t uncompensatedPressure() {
	uint8_t sampleRate = 3;

	uint8_t barometerRegister = 0xf4;
	int success = i2cWrite(barometerAddress, &barometerRegister, 1, 0xf4, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);
	
	// datasheet suggests a 25.5ms waiting period for a sample rate of 3
	usleep(25500);

	uint8_t dataRegisters[] = {0xf6, 0xf7, 0xf8};
	uint32_t pressure;
	success |= i2cWordRead(barometerAddress, dataRegisters, 3, &pressure, WORD_24_BIT, HIGH_BYTE_FIRST, AUTO_INCREMENT_ENABLED);
	if (success != 0) {
		printf("barometer pressure read failed");
	}
	// the bits need to be shifted down as per the datasheet
	else {
		//pressure <<= 8;
		pressure >>= (8 - sampleRate);
	}

	return pressure;
}

// gets the altitude relative to the take-off height
double barometerAltitude() {
	// initialize the sensors before using data
	initializeSensors();
	
	uint8_t sampleRate = 3;

	uint32_t uncompPressure = uncompensatedPressure();
	uint32_t uncompTemperature = uncompensatedTemperature();

	// the following calculations are taken from the datasheet
	int32_t X1 = (uncompTemperature - barometerCalibrationValues[5]) * barometerCalibrationValues[4] / 32768;
	int32_t X2 = (barometerCalibrationValues[9] * 2048) / (X1 + barometerCalibrationValues[10]);
	int32_t B5 = X1 + X2;
	//int32_t trueTemperature = (B5 + 8) / 16;

	int32_t B6 = B5 - 4000;
	X1 = (barometerCalibrationValues[7] * (B6 * B6 / 4096)) / 2048;
	X2 = barometerCalibrationValues[1] * B6 / 2048;
	int32_t X3 = X1 + X2;
	int32_t B3 = (((barometerCalibrationValues[0] * 4 + X3) << sampleRate) + 2) / 4;
	X1 = barometerCalibrationValues[2] * B6 / 8192;
	X2 = (barometerCalibrationValues[6] * (B6 * B6 / 4096)) / 65536;
	X3 = (X1 + X2 + 2) / 4;
	uint32_t B4 = barometerCalibrationValues[3] * (uint32_t)(X3 + 32768) / 32768;
	uint32_t B7 = ((uint32_t)uncompPressure - B3) * (50000 >> sampleRate);
	int32_t p;
	if (B7 < 0x80000000) {
		p = (B7 * 2) / B4;
	}
	else {
		p = (B7 / B4) * 2;
	}

	X1 = (p / 256) * (p / 256);
	X1 = (X1 * 3038) / 65536;
	X2 = (-7357 * p) / 65536;

	int32_t truePressure = p + (X1 + X2 + 3791) / 16;
	
	// now with the pressure and temperature calculated, we can used the formula
	//   provided in the data sheet for obtaining altitude based on the international
	//   barometric formula
	
	// pressure in pascals
	double seaLevelPressure = 101325;
	double altitude = 44330 * (1 - pow(((double)truePressure)/seaLevelPressure, (1.0/5.255)));

	return altitude;
}








