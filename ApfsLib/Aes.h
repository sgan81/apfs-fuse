#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief The AES class
 *
 * Class for AES en- and decryption. Supports all AES modes specified
 * in NIST FIPS-197.
 *
 * Several chaining modes are supported, namely ECB, CBC, CFB and OFB.
 *
 * Usage:
 *
 * @li Set a key with SetKey.
 * @li If necessary, set an IV with SetIV.
 * @li Encrypt / Decrypt your data with the desired functions.
 * @li When you are done, CleanUp behind you to make life harder for hackers.
 */
class AES
{
public:
	AES();
	~AES();

	/**
	 * @brief Encryption key size in bits.
	 */
	enum Mode {
		AES_128,
		AES_192,
		AES_256
	};

	/**
	 * @brief Clean Up
	 *
	 * Deletes all data which may contain key / plaintext material.
	 *
	 * @note After using this, a new key must be set before additional
	 * encryption is done.
	 */
	void CleanUp();

	/**
	 * @brief Set AES Key
	 *
	 * Sets the AES encryption key for this instance. Also sets the IV
	 * to 0.
	 *
	 * @param key Key data. Length depends on the mode parameter.
	 * @param mode Encryption mode (128, 192 or 256 bit key).
	 */
	void SetKey(const uint8_t *key, Mode mode);

	/**
	 * @brief Set Initialization Vector
	 *
	 * Set the initialization vector. This also resets the counter for the
	 * stream cipher modes (CFB, OFB).
	 *
	 * @param iv Initialization Vector. If NULL, a 0-vector is used.
	 */
	void SetIV(const uint8_t *iv);

	/**
	 * @brief Encrypt Block
	 *
	 * Encrypts a block of 16 bytes with the previously selected key (ECB mode).
	 *
	 * @param src Plaintext block (128 bits, 16 bytes)
	 * @param dst Encrypted block (128 bits, 16 bytes)
	 */
	void Encrypt(const void *src, void *dst);

	/**
	 * @brief Decrypt Block
	 *
	 * Decrypts a block of 16 bytes with the previously selected key (ECB mode).
	 *
	 * @param src Encrypted block (128 bits, 16 bytes)
	 * @param dst Decrypted block (128 bits, 16 bytes)
	 */
	void Decrypt(const void *src, void *dst);

	/**
	 * @brief Encrypt CBC
	 *
	 * Encrypt data in CBC mode.
	 *
	 * @param src Plaintext data.
	 * @param dst Encrypted data.
	 * @param size Number of bytes. Must be a multiple of 16.
	 */
	void EncryptCBC(const uint8_t *src, uint8_t *dst, size_t size);

	/**
	 * @brief Decrypt CBC
	 *
	 * Decrypt data in CBC mode.
	 *
	 * @param src Encrypted data.
	 * @param dst Decrypted data.
	 * @param size Number of bytes. Must be a multiple of 16.
	 */
	void DecryptCBC(const uint8_t *src, uint8_t *dst, size_t size);

	/**
	 * @brief Encrypt CFB
	 *
	 * Encrypt data in CFB mode.
	 *
	 * @param src Plaintext data.
	 * @param dst Encrypted data.
	 * @param size Number of bytes.
	 */
	void EncryptCFB(const uint8_t *src, uint8_t *dst, size_t size);

	/**
	 * @brief Decrypt CFB
	 *
	 * Decrypt data in CFB mode.
	 *
	 * @param src Encrypted data.
	 * @param dst Decrypted data.
	 * @param size Number of bytes.
	 */
	void DecryptCFB(const uint8_t *src, uint8_t *dst, size_t size);

	/**
	 * @brief Crypt OFB
	 *
	 * En- / Decrypt data in OFB mode.
	 *
	 * @param src Input data.
	 * @param dst Output data.
	 * @param size Number of bytes.
	 */
	void CryptOFB(const uint8_t *src, uint8_t *dst, size_t size);

private:
	/// Encryption round key.
	uint32_t _erk[60];
	/// Decryption round key.
	uint32_t _drk[60];
	/// Initialization vector.
	uint8_t _iv[16];
	/// Byte counter for CFB / OFB modes.
	int _tp;

	// AES constants (depending on mode)
	int Nk;
	int Nr;
	int Nb;

	// AES tables
	static const uint32_t Td0[256];
	static const uint32_t Td1[256];
	static const uint32_t Td2[256];
	static const uint32_t Td3[256];
	static const uint32_t Td4[256];
	static const uint32_t Te0[256];
	static const uint32_t Te1[256];
	static const uint32_t Te2[256];
	static const uint32_t Te3[256];
	static const uint32_t Te4[256];
	static const uint32_t rcon[11];

	/// Zero data
	static const uint8_t zeros[32];
};
