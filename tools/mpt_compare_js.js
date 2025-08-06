#!/usr/bin/env node

// MPT comparison script - JavaScript side
const { Trie } = require('@ethereumjs/trie');
const { keccak256 } = require('ethereum-cryptography/keccak');
const { bytesToHex, hexToBytes } = require('ethereum-cryptography/utils');
const { RLP } = require('@ethereumjs/rlp');
const fs = require('fs');

function hexToBigInt(hexStr) {
  if (hexStr === "0x" || hexStr === "0x00" || hexStr === "" || hexStr === "0") return 0n;
  return BigInt(hexStr);
}

function bigIntToMinimalBytes(value) {
  if (value === 0n) return new Uint8Array();
  const hex = value.toString(16);
  const paddedHex = hex.length % 2 === 0 ? hex : '0' + hex;
  return hexToBytes(paddedHex);
}

async function calculateStateRootFromJson(accountData) {
  const trie = new Trie();

  // Calculate empty storage root (for accounts with no storage)
  const emptyTrie = new Trie();
  const emptyStorageRoot = await emptyTrie.root();

  const processedAccounts = [];

  // For each account, encode and insert into state trie
  for (const [addressHex, account] of Object.entries(accountData)) {
    const address = hexToBytes(addressHex.slice(2)); // Remove 0x prefix
    const addressHash = keccak256(address);

    // Calculate code hash
    const code = account.code === "0x" ? new Uint8Array() : hexToBytes(account.code.slice(2));
    const codeHash = code.length > 0 ? keccak256(code) : keccak256(new Uint8Array());

    // Encode account: [nonce, balance, storageRoot, codeHash]
    const nonce = bigIntToMinimalBytes(hexToBigInt(account.nonce));
    const balance = bigIntToMinimalBytes(hexToBigInt(account.balance));

    // Handle storage (simplified for now - empty storage)
    let storageRoot = emptyStorageRoot;
    if (account.storage && Object.keys(account.storage).length > 0) {
      const storageTrie = new Trie();
      for (const [key, value] of Object.entries(account.storage)) {
        if (value !== "0x" && value !== "0x00") {
          const keyHash = keccak256(hexToBytes(key.slice(2)));
          const valueBytes = bigIntToMinimalBytes(hexToBigInt(value));
          const encodedValue = RLP.encode(valueBytes);
          await storageTrie.put(keyHash, encodedValue);
        }
      }
      storageRoot = await storageTrie.root();
    }

    const accountRLP = RLP.encode([nonce, balance, storageRoot, codeHash]);

    // Insert into state trie
    await trie.put(addressHash, accountRLP);

    processedAccounts.push({
      address: addressHex,
      addressHash: bytesToHex(addressHash),
      nonce: Array.from(nonce).map(b => b.toString(16).padStart(2, '0')).join(''),
      balance: Array.from(balance).map(b => b.toString(16).padStart(2, '0')).join(''),
      storageRoot: bytesToHex(storageRoot),
      codeHash: bytesToHex(codeHash),
      accountRLP: bytesToHex(accountRLP)
    });
  }

  const stateRoot = await trie.root();

  return {
    stateRoot: bytesToHex(stateRoot),
    accounts: processedAccounts
  };
}

async function main() {
  if (process.argv.length !== 3) {
    console.error('Usage: node mpt_compare_js.js <json_file>');
    process.exit(1);
  }

  const jsonFile = process.argv[2];

  try {
    const data = JSON.parse(fs.readFileSync(jsonFile, 'utf8'));
    const result = await calculateStateRootFromJson(data);

    console.log(JSON.stringify(result, null, 2));
  } catch (error) {
    console.error('Error:', error.message);
    process.exit(1);
  }
}

if (require.main === module) {
  main();
}