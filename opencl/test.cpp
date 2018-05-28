#include "logging.h"
#include <sstream>
#include <iomanip>
#include "gpu.h"
#include "cl/secp256k1_cpp.h"
#include "miscellaneous.h"
#include <chrono>
#include <assert.h>
#include "secp256k1_interface.h"


//Use CentralPU and GraphicsPU, CPU and GPU look too similar,
//causing unwanted typos.

Logger logTestCentralPU("../test/kanban_gpu/debug/logTestCentralPU.txt", "[test CPU] ");
Logger logTestGraphicsPU("../test/kanban_gpu/debug/logTestGraphicsPU.txt", "[test GPU] ");
extern Logger logServer;

class testSignatures {
public:
  std::vector<unsigned char> messages;
  std::vector<unsigned char> nonces;
  std::vector<unsigned char> secretKeys;
  std::vector<std::string> secretStrings;
  std::vector<unsigned char> outputSignatures;
  std::vector<unsigned char> outputSignatureSizes;
  std::vector<std::string> outputSignatureStrings;
  std::vector<unsigned char> publicKeysBuffer;
  std::vector<unsigned char> publicKeysSizes;
  std::vector<std::string> publicKeyStrings;
  std::vector<unsigned char> outputVerifications;

  unsigned numMessagesPerPipeline;
  bool flagInitialized;
  testSignatures() {
    this->flagInitialized = false;
  }
  void initialize();
  bool testSign(GPU& theGPU);
  bool testPublicKeys(GPU& theGPU);
  bool testVerifySignatures(GPU& theGPU, bool tamperWithSignature);
  std::string toStringPublicKeys();
  std::string toStringSignatures();
};

class testerSHA256 {
public:
  std::vector<std::vector<std::string> > knownSHA256s;
  std::string inputBuffer;
  std::vector<unsigned char> outputBuffer;
  std::vector<uint> messageStarts;
  std::vector<uint> messageLengths;
  std::vector<unsigned char> messageStartsUChar;
  std::vector<unsigned char> messageLengthsUChar;
  bool flagInitialized;
  testerSHA256() {
    this->flagInitialized = false;
  }
  void initialize();
  bool testSHA256(GPU& theGPU);
  unsigned totalToCompute;
};

void readStringsFromBufferWithSizes(
  const std::vector<unsigned char>& inputBuffer,
  const std::vector<unsigned char>& inputSizes,
  unsigned int oneBufferMaxSize,
  std::vector<std::string>& output
) {
  output.resize(inputSizes.size() / 4);
  for (unsigned i = 0; i< output.size(); i ++) {
    unsigned int currentSize = memoryPool_read_uint(&inputSizes[4 * i]);
    output[i].assign((char *) &inputBuffer[i * oneBufferMaxSize], currentSize);
  }
}

Logger& getAppropriateLogger(GPU& theGPU) {
  Logger& result = theGPU.theDesiredDeviceType == CL_DEVICE_TYPE_CPU ? logTestCentralPU : logTestGraphicsPU;
  if (theGPU.theDesiredDeviceType == CL_DEVICE_TYPE_CPU) {
    result << "Central PU" << Logger::endL;
  } else {
    result << "Graphics PU" << Logger::endL;
  }
  return result;
}

void printComments(unsigned char* comments) {
  std::string resultString((char*) comments, 999);
  char returnGPU = resultString[0];
  std::string extraMessage1 = resultString.substr(1, 32);
  std::string extraMessage2 = resultString.substr(33, 32);
  std::string extraMessage3 = resultString.substr(65, 32);
  logServer << "Return of verification: " << (int) returnGPU << Logger::endL;
  logServer << "extraMessage xr: " << Miscellaneous::toStringHex(extraMessage1) << Logger::endL;
  logServer << "extraMessage pr.x: " << Miscellaneous::toStringHex(extraMessage2) << Logger::endL;
  logServer << "extraMessage pr.z: " << Miscellaneous::toStringHex(extraMessage3) << Logger::endL;
}

void testPrintMemoryPoolGeneral(const unsigned char* theMemoryPool, const std::string& computationID, Logger& logTest) {
  logTest << computationID << Logger::endL;
  std::string memoryPoolPrintout;
  //int useFulmemoryPoolSize = 16 * 64 * 64 + 10192 + 100;
  logTest << "Claimed max size: " << memoryPool_readMaxPoolSize(theMemoryPool) << Logger::endL;
  logTest << "Used: " << memoryPool_readPoolSize(theMemoryPool) << Logger::endL;
  logTest << "Memory pool reserved bytes: " << std::dec << memoryPool_readNumberReservedBytesExcludingLog() << Logger::endL;
  logTest << "Memory pool reserved bytes + log size: " << std::dec << memoryPool_readNumberReservedBytesIncludingLog() << Logger::endL;
  logTest << "Memory pool pointer: 0x" << std::hex << ((long) theMemoryPool) << Logger::endL;
  int initialBytesToPrint = memoryPool_readNumberReservedBytesIncludingLog() + 1000;
  logTest << "First " << std::dec << initialBytesToPrint << " hex-formatted characters of the memory pool: " << Logger::endL;
  memoryPoolPrintout.assign((const char*) theMemoryPool, initialBytesToPrint);
  logTest << Miscellaneous::toStringHex(memoryPoolPrintout) << Logger::endL;
  for (int i = 0; i < MACRO_numberOfOutputs; i ++) {
    logTest << "Debug " << i << ": " << toStringOutputObject(i, theMemoryPool) << Logger::endL;
  }
  logTest << "Computation log:\n"
  << toStringErrorLog(theMemoryPool) << Logger::endL << Logger::endL;
}

void testPrintMultiplicationContext(const unsigned char* theMemoryPool, const std::string& computationID, Logger& logTest) {
  testPrintMemoryPoolGeneral(theMemoryPool, computationID, logTest);
  uint32_t outputPosition = memoryPool_read_uint_fromOutput(0, theMemoryPool);
  logTest << "Position multiplication context: " << outputPosition << Logger::endL;
  secp256k1_ecmult_context multiplicationContext;
  secp256k1_ecmult_context_init(&multiplicationContext);
  memoryPool_read_multiplicationContext_PORTABLE(&multiplicationContext, theMemoryPool);
  logTest << "Multiplication context:\n"
  << toStringSecp256k1_MultiplicationContext(multiplicationContext, false) << Logger::endL;
}

void testPrintGeneratorContext(const unsigned char* theMemoryPool, const std::string& computationID, Logger& logTest) {
  testPrintMemoryPoolGeneral(theMemoryPool, computationID, logTest);
  uint32_t outputPositionGeneratorContextStruct = memoryPool_read_uint_fromOutput(0, theMemoryPool);
  uint32_t outputPositionGeneratorContextContent = memoryPool_read_uint_fromOutput(1, theMemoryPool);
  logTest << "Context struct position: " << outputPositionGeneratorContextStruct << Logger::endL;
  logTest << "Context content position: " << outputPositionGeneratorContextContent << Logger::endL;
  secp256k1_ecmult_gen_context theGeneratorContext;
  memoryPool_read_generatorContext_PORTABLE(&theGeneratorContext, theMemoryPool);
  logTest << "Generator context:\n" << toStringSecp256k1_GeneratorContext(theGeneratorContext, false) << Logger::endL;
}

extern void secp256k1_opencl_compute_multiplication_context(
  __global unsigned char* outputMemoryPoolContainingMultiplicationContext
);

extern void secp256k1_opencl_compute_generator_context(
  __global unsigned char* outputMemoryPoolContainingGeneratorContext
);

bool testMainPart1ComputeContexts(GPU& theGPU) {
  //*****CPU tests*******
  if (!CryptoEC256k1::computeMultiplicationContextDefaultBuffers())
    return false;
  /////////////////////////////
  if (!CryptoEC256k1::computeGeneratorContextDefaultBuffers())
    return false;
  logTestCentralPU << "Generator context computed. " << Logger::endL;
  testPrintGeneratorContext(CryptoEC256k1::bufferGeneratorContext, "Central PU", logTestCentralPU);
  /////////////////////////////


  /////////////////////////////
  logTestCentralPU << "Multiplication context computed. " << Logger::endL;
  testPrintMultiplicationContext(CryptoEC256k1::bufferMultiplicationContext, "Central PU", logTestCentralPU);
  /////////////////////////////

  //*****GPU tests*******
  /////////////////////////////
  if (!CryptoEC256k1GPU::computeGeneratorContextDefaultBuffers(theGPU))
    return false;
  logTestGraphicsPU << "Generator context computed. " << Logger::endL;
  std::string idOpenCL;
  if (theGPU.theDesiredDeviceType == CL_DEVICE_TYPE_GPU) {
    idOpenCL = "Graphics PU";
  } else {
    idOpenCL = "openCL CPU";
  }

  testPrintGeneratorContext(theGPU.bufferGeneratorContext, "Graphics PU", logTestGraphicsPU);
  /////////////////////////////


  /////////////////////////////
  if (!CryptoEC256k1GPU::computeMultiplicationContextDefaultBuffers(theGPU))
    return false;
  logTestGraphicsPU << "Multiplication context computed. " << Logger::endL;
  testPrintMultiplicationContext(theGPU.bufferMultiplicationContext, "Graphics PU", logTestGraphicsPU);
  /////////////////////////////


  return true;
}

void PublicKey::reset() {
  for (int i = 0; i < this->maxSerializationSize; i ++) {
    this->serialization[i] = 0;
  }
  this->size = 0;
}

std::string PublicKey::toString() {
  std::string buffer;
  buffer.assign((const char*) this->serialization, this->size);
  return Miscellaneous::toStringHex(buffer);
}

void GeneratorScalar::ComputeScalarFromSerialization() {
  secp256k1_scalar_set_b32(&this->scalar, this->serialization, NULL);
}

void GeneratorScalar::TestAssignString(const std::string& input) {
  int lastCopiedIndex;
  int numCharacters = std::min((int) input.size(), 32);
  for (lastCopiedIndex = 0; lastCopiedIndex < numCharacters; lastCopiedIndex ++) {
    this->serialization[lastCopiedIndex] = input[lastCopiedIndex];
  }
  for (; lastCopiedIndex < 32; lastCopiedIndex ++) {
    this->serialization[lastCopiedIndex] = 0;
  }
  this->ComputeScalarFromSerialization();
}

std::string Signature::toString() {
  std::stringstream out;
  out << "(r,s): " << toStringSecp256k1_Scalar(this->r) << ", " << toStringSecp256k1_Scalar(this->s);
  return out.str();
}

bool Signature::ComputeScalarsFromSerialization() {
  if (secp256k1_ecdsa_sig_parse(&this->r, & this->s, this->serialization, this->size) == 0) {
    return false;
  }
  return true;
}

void Signature::reset() {
  for (int i = 0; i < 8; i ++) {
    this->r.d[i] = 0;
  }
  for (int i = 0; i < 8; i ++) {
    this->s.d[i] = 0;
  }
  for (int i = 0; i < this->maxSerializationSize; i ++) {
    this->serialization[i] = 0;
  }
}

bool testMainPart2Signatures(GPU& theGPU) {
  Signature theSignature;
  PrivateKey theKey;
  GeneratorScalar message;
  theKey.key.TestAssignString("This is a secret. ");
  message.TestAssignString("This is a message. ");
  theKey.nonceMustChangeAfterEverySignature.TestAssignString("This is a nonce. ");
  theSignature.reset();
  CryptoEC256k1::signMessageDefaultBuffers(
    theSignature.serialization,
    &theSignature.size,
    theKey.nonceMustChangeAfterEverySignature.serialization,
    theKey.key.serialization,
    message.serialization
  );
  theSignature.ComputeScalarsFromSerialization();
  logTestCentralPU << "Signature:\n" << theSignature.toString() << Logger::endL;
  PublicKey thePublicKey;

  CryptoEC256k1::generatePublicKeyDefaultBuffers(
    thePublicKey.serialization,
    &thePublicKey.size,
    theKey.key.serialization
  );
  logTestCentralPU << "Public key:\n" << thePublicKey.toString() << Logger::endL;
  //getMultiplicationContext(bufferCentralPUMultiplicationContext, multiplicationContext);
  unsigned char signatureResult[1];
  signatureResult[0] = 3;
  CryptoEC256k1::verifySignatureDefaultBuffers(
    &signatureResult[0],
    theSignature.serialization,
    theSignature.size,
    thePublicKey.serialization,
    thePublicKey.size,
    message.serialization
  );
  logTestCentralPU << "Verification of signature (expected 1): " << (int) signatureResult[0] << Logger::endL;
  theSignature.serialization[4] = 5;
  signatureResult[0] = 3;
  CryptoEC256k1::verifySignatureDefaultBuffers(
    &signatureResult[0],
    theSignature.serialization,
    theSignature.size,
    thePublicKey.serialization,
    thePublicKey.size,
    message.serialization
  );
  logTestCentralPU << "Verification of a signature that's been tampered with (expected 0): " << (int) signatureResult[0] << Logger::endL;
  theSignature.reset();
  CryptoEC256k1GPU::signMessageDefaultBuffers(
    theSignature.serialization,
    &theSignature.size,
    theKey.nonceMustChangeAfterEverySignature.serialization,
    theKey.key.serialization,
    message.serialization,
    0,
    theGPU
  );
  theSignature.ComputeScalarsFromSerialization();
  logTestGraphicsPU << "Signature:\n" << theSignature.toString() << Logger::endL;
  thePublicKey.reset();
  if (!CryptoEC256k1GPU::generatePublicKeyDefaultBuffers(
    thePublicKey.serialization,
    &thePublicKey.size,
    theKey.key.serialization,
    theGPU
  )) {
    logTestGraphicsPU << "ERROR: generatePublicKey returned false. " << Logger::endL;
  }
  logTestGraphicsPU << "Public key:\n" << thePublicKey.toString() << Logger::endL;
  signatureResult[0] = 3;
  if (!CryptoEC256k1GPU::verifySignatureDefaultBuffers(
    &signatureResult[0],
    theSignature.serialization,
    theSignature.size,
    thePublicKey.serialization,
    thePublicKey.size,
    message.serialization,
    theGPU
  )) {
    logTestGraphicsPU << "ERROR: verifySignature returned false. " << Logger::endL;
  }
  logTestGraphicsPU << "Verification of signature (expected 1): " << (int) signatureResult[0] << Logger::endL;
  theSignature.serialization[4] = 5;
  signatureResult[0] = 3;
  if (!CryptoEC256k1GPU::verifySignatureDefaultBuffers(
    &signatureResult[0],
    theSignature.serialization,
    theSignature.size,
    thePublicKey.serialization,
    thePublicKey.size,
    message.serialization,
    theGPU
  )) {
    logTestGraphicsPU << "ERROR: verifySignature returned false. " << Logger::endL;
  }
  logTestGraphicsPU << "Verification of a signature that's been tampered with (expected 0): " << (int) signatureResult[0] << Logger::endL;
  return true;
}

bool testBasicOperations(GPU& theGPU){
  CryptoEC256k1::testSuite1BasicOperationsDefaultBuffers();
  testPrintMemoryPoolGeneral(CryptoEC256k1::bufferTestSuite1BasicOperations, "Central PU", logTestCentralPU);

  CryptoEC256k1GPU::testSuite1BasicOperationsDefaultBuffers(theGPU);
  testPrintMemoryPoolGeneral(theGPU.bufferTestSuite1BasicOperations, "Graphics PU", logTestGraphicsPU);
  return true;
}

bool testGPU(GPU& inputGPU) {
  //if (!testBasicOperations(theGPU))
  //  return - 1;
  //if (!testMainPart1ComputeContexts(inputGPU))
  //  return false;
  //if (!testMainPart2Signatures(inputGPU))
  //  return false;
  //testerSHA256 theSHA256Tester;
  //if (!theSHA256Tester.testSHA256(inputGPU)) {
  //  return false;
  //}
  testSignatures theSignatureTest;
  if (!theSignatureTest.testPublicKeys(inputGPU)) {
    return false;
  }
  if (!theSignatureTest.testSign(inputGPU)) {
    return false;
  }
  //if (!theSignatureTest.testVerifySignatures(inputGPU)) {
  //  return false;
  //}
  return true;
}

int testMain() {
  GPU theGPU;
  GPU theOpenCLCPU;
  theOpenCLCPU.theDesiredDeviceType = CL_DEVICE_TYPE_CPU;
  if (! testGPU(theGPU)) {
    return - 1;
  }
  if (! testGPU(theOpenCLCPU)) {
    return - 1;
  }
  return 0;
}

void testerSHA256::initialize() {
  if (this->flagInitialized) {
    return;
  }
  this->flagInitialized = true;
  this->totalToCompute = 100000;
  this->outputBuffer.resize(12000000);
  this->knownSHA256s.push_back((std::vector<std::string>) {
    "abc",
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
  });
  this->knownSHA256s.push_back((std::vector<std::string>) {
    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
  });
  this->knownSHA256s.push_back((std::vector<std::string>) {
   "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
   "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"
  });
  this->inputBuffer.reserve(100 * this->totalToCompute);
  this->inputBuffer.clear();
  this->messageStartsUChar.resize(4 * this->totalToCompute);
  this->messageLengthsUChar.resize(4 * this->totalToCompute);
  for (unsigned i = 0; i < this->totalToCompute; i ++) {
    unsigned testCounter = i % this->knownSHA256s.size();
    std::string& currentMessage = this->knownSHA256s[testCounter][0];
    this->messageStarts.push_back(this->inputBuffer.size());
    this->messageLengths.push_back(currentMessage.size());

    memoryPool_write_uint((uint32_t) this->inputBuffer.size(), &(this->messageStartsUChar[i * 4]));
    memoryPool_write_uint((uint32_t) currentMessage.size(), &(this->messageLengthsUChar[i * 4]));

    this->inputBuffer.append(currentMessage);
    for (unsigned j = 0; j < 32; j ++) {
      this->outputBuffer[i * 32 + j] = 0;
    }
  }
  //for (unsigned i = 0; i < 100 ; i++)
  //  std::cout << "Byte " << i << ": " << (int) this->messageLengthsUChar[i] << std::endl;
  //for (unsigned i = 0; i < 100 ; i++)
  //  std::cout << "Offset Byte " << i << ": " << (int) this->messageStartsUChar[i] << std::endl;
  //for (unsigned i = 0; i < 100 ; i++)
  //  std::cout << "message Byte " << i << ": " << (int) this->inputBuffer[i] << std::endl;
}

bool testerSHA256::testSHA256(GPU& theGPU) {
  // Create the two input vectors
  Logger& theTestLogger = getAppropriateLogger(theGPU);
  theTestLogger << "Running SHA256 benchmark. " << Logger::endL;
  std::shared_ptr<GPUKernel> theKernel = theGPU.getKernel(GPU::kernelSHA256);
  if (!theKernel->build()) {
    std::cout << "DEBUG: failed to build sha kernel. " << std::endl;
    assert(false);
  }
  this->initialize();

  auto timeStart = std::chrono::system_clock::now();
  uint32_t largeTestCounter = 0;
  uint32_t grandTotal = 0;
  if (!theKernel->writeToBuffer(4, this->inputBuffer)) {
    theTestLogger << "Bad write" << Logger::endL;
    assert(false);
  }
  if (!theKernel->writeToBuffer(1, this->messageStartsUChar)){
    theTestLogger << "Bad write" << Logger::endL;
    assert(false);
  }
  if (!theKernel->writeToBuffer(2, this->messageLengthsUChar)) {
    theTestLogger << "Bad write" << Logger::endL;
    assert(false);
  }
  int numPasses = 10;
  cl_mem& result = theKernel->getOutput(0)->theMemory;
  for (int i = 0; i < numPasses; i++) {
    for (largeTestCounter = 0; largeTestCounter < this->totalToCompute; largeTestCounter ++) {
      grandTotal ++;
      if (!theKernel->writeArgument(3, largeTestCounter)) {
        theTestLogger << "Bad write" << Logger::endL;
        assert(false);
      }
      //theKernel->writeToBuffer(0, &theLength, sizeof(uint));
      //std::cout << "DEBUG: Setting arguments ... " << std::endl;
      //std::cout << "DEBUG: arguments set, enqueueing kernel... " << std::endl;
      cl_int ret = clEnqueueNDRangeKernel(
        theGPU.commandQueue,
        theKernel->kernel,
        3,
        NULL,
        theKernel->global_item_size,
        theKernel->local_item_size,
        0,
        NULL,
        NULL
      );
      if (ret != CL_SUCCESS) {
        theTestLogger << "Failed to enqueue kernel. Return code: " << ret << ". " << Logger::endL;
        return false;
      }
      //std::cout << "DEBUG: kernel enqueued, proceeding to read buffer. " << std::endl;
      if (largeTestCounter % 500 == 0) {
        auto timeCurrent = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = timeCurrent - timeStart;
        std::cout << "Scheduled grand total: " << grandTotal << " sha256s in " << elapsed_seconds.count() << " second(s). " << std::endl;
      }
    }
    //theTestLogger << "Total to extract: " << 32 * theSHA256Test.totalToCompute << Logger::endL;
    cl_int ret = clEnqueueReadBuffer (
      theGPU.commandQueue,
      result,
      CL_TRUE,
      0,
      32 * this->totalToCompute,
      &this->outputBuffer[0],
      0,
      NULL,
      NULL
    );
    if (ret != CL_SUCCESS) {
      theTestLogger << "Failed to enqueue read buffer. Return code: " << ret << ". " << Logger::endL;
      return false;
    }
  }
  auto timeCurrent = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed_seconds = timeCurrent - timeStart;
  theTestLogger << "Computed " << grandTotal
  << " sha256s in " << elapsed_seconds.count() << " second(s). " << Logger::endL;
  theTestLogger << "Speed: " << (grandTotal / elapsed_seconds.count()) << " hashes per second. " << Logger::endL;
  theTestLogger << "Checking computations ..." << Logger::endL;
  for (largeTestCounter = 0; largeTestCounter < this->totalToCompute; largeTestCounter ++) {
    unsigned testCounteR = largeTestCounter % this->knownSHA256s.size();
    std::stringstream out;
    unsigned offset = largeTestCounter * 32;
    for (unsigned i = offset; i < offset + 32; i ++) {
      out << std::hex << std::setw(2) << std::setfill('0') << ((int) ((unsigned) this->outputBuffer[i]));
    }
    if (out.str() != this->knownSHA256s[testCounteR][1]) {
      theTestLogger << "\e[31mSha of message index " << largeTestCounter
      << ": " << this->knownSHA256s[testCounteR][0] << " is wrongly computed to be: " << out.str()
      << " instead of: " << this->knownSHA256s[testCounteR][1] << "\e[39m" << Logger::endL;
      assert(false);
      return false;
    }
  }
  theTestLogger << "Success!" << Logger::endL;
  std::cout << "\e[32mSuccess!\e[39m" << std::endl;
  return true;
}

unsigned char getByte(unsigned char byte1, unsigned char byte2, unsigned char byte3) {
  return byte1 * byte1 * (byte1 + 3) + byte2 * 7 + byte3 * 3 + 5 + byte1 * byte3;
}

void testSignatures::initialize() {
  if (this->flagInitialized) {
    return;
  }
  this->flagInitialized = true;
  this->numMessagesPerPipeline = 1000;
  std::cout << "DEBUG: got to here, pt 1" << std::endl;
  unsigned totalPipelineSize = this->numMessagesPerPipeline * 32;
  this->messages.resize(totalPipelineSize);
  this->nonces.resize(totalPipelineSize);
  this->secretKeys.resize(totalPipelineSize);
  this->secretStrings.resize(this->numMessagesPerPipeline);
  ////////////////////
  this->outputSignatures.resize(this->numMessagesPerPipeline * MACRO_size_of_signature);
  this->outputSignatureSizes.resize(this->numMessagesPerPipeline * 4);
  this->outputSignatureStrings.resize(this->numMessagesPerPipeline);
  ////////////////////
  this->publicKeysBuffer.resize(this->numMessagesPerPipeline * MACRO_size_of_signature);
  this->publicKeysSizes.resize(this->numMessagesPerPipeline * 4);
  this->publicKeyStrings.resize(this->numMessagesPerPipeline);
  ////////////////////
  this->outputVerifications.resize(this->numMessagesPerPipeline);
  std::cout << "DEBUG: got to here, pt 2" << std::endl;
  this->messages[0] = 'a';
  this->messages[1] = 'b';
  this->messages[2] = 'c';
  this->nonces[0] = 'e';
  this->nonces[1] = 'f';
  this->nonces[2] = 'g';
  this->secretKeys[0] = 'h';
  this->secretKeys[1] = 'i';
  this->secretKeys[2] = 'j';
  for (unsigned i = 3; i < totalPipelineSize; i ++) {
    this->messages[i] = getByte(this->messages[i - 1], this->messages[i - 2], this->nonces[i - 3]);
    this->nonces[i] = getByte(this->nonces[i - 1], this->nonces[i - 2], this->secretKeys[i - 3]);
    this->secretKeys[i] = getByte(this->secretKeys[i - 1], this->secretKeys[i - 2], this->messages[i - 3]);
  }
  std::cout << "DEBUG: got to here, pt 3" << std::endl;
  for (unsigned i = 0; i < this->numMessagesPerPipeline; i ++) {
    this->secretStrings[i].assign((char*) &this->secretKeys[i * 32], 32);
  }
}

bool testSignatures::testSign(GPU& theGPU) {
  Logger& testLogger = getAppropriateLogger(theGPU);
  testLogger << "Running signature benchmark. " << Logger::endL;
  // Create the two input vectors
  theGPU.initializeAllNoBuild();
  this->initialize();
  if (!CryptoEC256k1GPU::initializeGeneratorContext(theGPU)) {
    return false;
  }
  // Create a command queue
  std::shared_ptr<GPUKernel> kernelSign = theGPU.theKernels[GPU::kernelSign];
  if (!kernelSign->build()) {
    return false;
  }
  std::cout << "DEBUG: about to write to buffer. " << std::endl;

  auto timeStart = std::chrono::system_clock::now();
  unsigned counterTest;
  //__global unsigned char* outputSignature,
  //__global unsigned char* outputSizes,
  //__global unsigned char* outputInputNonce,
  //__global unsigned char* inputSecretKey,
  //__global unsigned char* inputMessage,
  //__global unsigned char* inputMemoryPoolGeneratorContext,
  //unsigned int messageIndexChar
  kernelSign->writeToBuffer(2, this->nonces);
  kernelSign->writeToBuffer(3, this->secretKeys);
  kernelSign->writeToBuffer(4, this->messages);
  for (counterTest = 0; counterTest < this->numMessagesPerPipeline; counterTest ++) {
    kernelSign->writeArgument(6, counterTest);
    //theKernel->writeToBuffer(0, &theLength, sizeof(uint));
    //std::cout << "DEBUG: Setting arguments ... " << std::endl;
    //std::cout << "DEBUG: arguments set, enqueueing kernel... " << std::endl;
    cl_int ret = clEnqueueNDRangeKernel(
      theGPU.commandQueue,
      kernelSign->kernel,
      1,
      NULL,
      kernelSign->global_item_size,
      kernelSign->local_item_size,
      0,
      NULL,
      NULL
    );
    if (ret != CL_SUCCESS) {
      testLogger << "Failed to enqueue kernel. Return code: " << ret << ". " << Logger::endL;
      return false;
    }
    //std::cout << "DEBUG: kernel enqueued, proceeding to read buffer. " << std::endl;
    if (counterTest % 100 == 0) {
      auto timeCurrent = std::chrono::system_clock::now();
      std::chrono::duration<double> elapsed_seconds = timeCurrent - timeStart;
      std::cout << "Scheduled " << counterTest << " 32-byte messages in " << elapsed_seconds.count() << " second(s),"
      << " current speed: "
      << ((counterTest + 1) / elapsed_seconds.count()) << " signature(s) per second." << std::endl;
    }
  }
  cl_mem& resultBuffers = kernelSign->getOutput(0)->theMemory;
  cl_int ret = clEnqueueReadBuffer (
    theGPU.commandQueue,
    resultBuffers,
    CL_TRUE,
    0,
    this->outputSignatures.size(),
    &this->outputSignatures[0],
    0,
    NULL,
    NULL
  );
  if (ret != CL_SUCCESS) {
    testLogger << "Failed to enqueue read buffer. Return code: " << ret << ". " << Logger::endL;
    return false;
  }
  cl_mem& resultSizes = kernelSign->getOutput(1)->theMemory;
  ret = clEnqueueReadBuffer (
    theGPU.commandQueue,
    resultSizes,
    CL_TRUE,
    0,
    this->outputSignatureSizes.size(),
    &this->outputSignatureSizes[0],
    0,
    NULL,
    NULL
  );
  if (ret != CL_SUCCESS) {
    testLogger << "Failed to enqueue read buffer. Return code: " << ret << ". " << Logger::endL;
    return false;
  }

  auto timeCurrent = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed_seconds = timeCurrent - timeStart;
  testLogger << "Signed " << counterTest << " 32-byte messages in " << elapsed_seconds.count() << " second(s). " << Logger::endL;
  testLogger << "Speed: "
  << (this->numMessagesPerPipeline / elapsed_seconds.count()) << " signature(s) per second." << Logger::endL;
  readStringsFromBufferWithSizes(
    this->outputSignatures,
    this->outputSignatureSizes,
    MACRO_size_of_signature,
    this->outputSignatureStrings
  );
  testLogger << this->toStringSignatures() << Logger::endL;
  return true;
}

bool testSignatures::testPublicKeys(GPU& theGPU) {
  Logger& theTestLogger = getAppropriateLogger(theGPU);
  theTestLogger << "Running public key generation benchmark. " << Logger::endL;
  this->initialize();
  theGPU.initializeAllNoBuild();
  // Create a command queue
  std::shared_ptr<GPUKernel> kernelPublicKeys = theGPU.theKernels[GPU::kernelGeneratePublicKey];
  if (!kernelPublicKeys->build()) {
    return false;
  }
  CryptoEC256k1GPU::computeGeneratorContextDefaultBuffers(theGPU);

  std::cout << "DEBUG: about to write to buffer. " << std::endl;

  auto timeStart = std::chrono::system_clock::now();
  unsigned counterTest = - 1;

  kernelPublicKeys->writeToBuffer(2, this->secretKeys);
  for (counterTest = 0; counterTest < this->numMessagesPerPipeline; counterTest ++) {
    kernelPublicKeys->writeArgument(4, counterTest);
    //theKernel->writeToBuffer(0, &theLength, sizeof(uint));
    //std::cout << "DEBUG: Setting arguments ... " << std::endl;
    //std::cout << "DEBUG: arguments set, enqueueing kernel... " << std::endl;
    cl_int ret = clEnqueueNDRangeKernel(
      theGPU.commandQueue,
      kernelPublicKeys->kernel,
      1,
      NULL,
      kernelPublicKeys->global_item_size,
      kernelPublicKeys->local_item_size,
      0,
      NULL,
      NULL
    );
    if (ret != CL_SUCCESS) {
      theTestLogger << "Failed to enqueue kernel. Return code: " << ret << ". " << Logger::endL;
      return false;
    }
    //std::cout << "DEBUG: kernel enqueued, proceeding to read buffer. " << std::endl;
    if (counterTest % 100 == 0) {
      auto timeCurrent = std::chrono::system_clock::now();
      std::chrono::duration<double> elapsed_seconds = timeCurrent - timeStart;
      std::cout << "Scheduled " << counterTest << " 32-byte messages in " << elapsed_seconds.count() << " second(s),"
      << " current speed: "
      << ((counterTest + 1) / elapsed_seconds.count()) << " public keys per second." << std::endl;
    }
  }
  theTestLogger << "Fetching public keys... " << Logger::endL;
  cl_mem& resultKeys = kernelPublicKeys->getOutput(0)->theMemory;
  cl_int ret = clEnqueueReadBuffer (
    theGPU.commandQueue,
    resultKeys,
    CL_TRUE,
    0,
    this->publicKeysBuffer.size(),
    &this->publicKeysBuffer[0],
    0,
    NULL,
    NULL
  );
  if (ret != CL_SUCCESS) {
    theTestLogger << "Failed to enqueue read buffer. Return code: " << ret << ". " << Logger::endL;
    return false;
  }
  theTestLogger << "Public keys fetched, fetching sizes... " << Logger::endL;
  cl_mem& resultKeySizes = kernelPublicKeys->getOutput(1)->theMemory;
  ret = clEnqueueReadBuffer (
    theGPU.commandQueue,
    resultKeySizes,
    CL_TRUE,
    0,
    this->publicKeysSizes.size(),
    &this->publicKeysSizes[0],
    0,
    NULL,
    NULL
  );
  if (ret != CL_SUCCESS) {
    theTestLogger << "Failed to enqueue read buffer. Return code: " << ret << ". " << Logger::endL;
    return false;
  }
  theTestLogger << "Public keys sizes fetched. " << Logger::endL;
  auto timeCurrent = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed_seconds = timeCurrent - timeStart;
  theTestLogger << "Generated " << counterTest << " public keys in " << elapsed_seconds.count() << " second(s). "
  << Logger::endL;
  theTestLogger << "Speed: "
  << (this->numMessagesPerPipeline / elapsed_seconds.count()) << " signature(s) per second." << Logger::endL;
  readStringsFromBufferWithSizes(
    this->publicKeysBuffer,
    this->publicKeysSizes,
    MACRO_size_of_signature,
    this->publicKeyStrings
  );
  for (counterTest = 0; counterTest < this->numMessagesPerPipeline; counterTest ++) {
    if (this->publicKeyStrings.size() == 0) {
      theTestLogger << "Public key: " << counterTest << " failed to be generated. " << Logger::endL;
      assert(false);
    }
  }
  theTestLogger << this->toStringPublicKeys() << Logger::endL;
  return true;
}

std::string testSignatures::toStringPublicKeys() {
  std::stringstream out;
  out << this->publicKeyStrings.size() << " public keys generated.\n";
  unsigned numSignaturesToShowOnEachEnd = 1005;
  for (unsigned i = 0; i < this->publicKeyStrings.size(); i ++) {
    if (i == numSignaturesToShowOnEachEnd) {
      out << "...\n";
    }
    if (i >= numSignaturesToShowOnEachEnd && i < this->publicKeyStrings.size() - numSignaturesToShowOnEachEnd) {
      continue;
    }
    out << "secret_" << i << ": " << Miscellaneous::toStringHex(this->secretStrings[i]) << ", ";
    out << "pk_" << i << ": " << Miscellaneous::toStringHex(this->publicKeyStrings[i]) << "\n";
  }
  return out.str();
}

std::string testSignatures::toStringSignatures() {
  std::stringstream out;
  out << this->outputSignatureStrings.size() << " signatures generated.\n";
  unsigned numSignaturesToShowOnEachEnd = 1005;
  for (unsigned i = 0; i < this->outputSignatureStrings.size(); i ++) {
    if (i == numSignaturesToShowOnEachEnd) {
      out << "...\n";
    }
    if (i >= numSignaturesToShowOnEachEnd && i < this->outputSignatureStrings.size() - numSignaturesToShowOnEachEnd) {
      continue;
    }
    out << "sig_" << i << ": " << Miscellaneous::toStringHex(this->outputSignatureStrings[i]) << "\n";
  }
  return out.str();
}

bool testSignatures::testVerifySignatures(GPU& theGPU, bool tamperWithSignature) {
  Logger& theTestLogger = getAppropriateLogger(theGPU);
  theTestLogger << "Running signature verification benchmark. " << Logger::endL;
  this->initialize();
  theGPU.initializeAllNoBuild();
  // Create a command queue
  std::shared_ptr<GPUKernel> kernelVerify = theGPU.theKernels[GPU::kernelVerifySignature];
  if (!CryptoEC256k1GPU::initializeMultiplicationContext(theGPU)) {
    return false;
  }
  if (!CryptoEC256k1GPU::initializeGeneratorContext(theGPU)) {
    return false;
  }
  if (!kernelVerify->build()) {
    return false;
  }

  auto timeStart = std::chrono::system_clock::now();
  unsigned counterTest = - 1;
  //openCL function arguments:
  //__global unsigned char *output,
  //__global unsigned char *outputMemoryPoolSignature,
  //__global const unsigned char* inputSignature,
  //__global const unsigned char* signatureSizes,
  //__global const unsigned char* publicKey,
  //__global const unsigned char* publicKeySizes,
  //__global const unsigned char* message,
  //__global const unsigned char* memoryPoolMultiplicationContext,
  //unsigned int messageIndex
  if (tamperWithSignature){
    theTestLogger << "Testing good signatures. All signatures should be verified. " << Logger::endL;
  } else {
    theTestLogger << "Testing signatures that have been tampered with. All signatures should be invalidated. " << Logger::endL;
  }
  if (tamperWithSignature) {
    for (unsigned i = 0; i < this->numMessagesPerPipeline; i ++) {
      this->outputSignatures[i * MACRO_size_of_signature + 11] ++;
    }
  }
  kernelVerify->writeToBuffer(2, this->outputSignatures);
  kernelVerify->writeToBuffer(3, this->outputSignatureSizes);
  kernelVerify->writeToBuffer(4, this->publicKeysBuffer);
  kernelVerify->writeToBuffer(5, this->publicKeysSizes);
  kernelVerify->writeToBuffer(6, this->messages);
  for (counterTest = 0; counterTest < this->numMessagesPerPipeline; counterTest ++) {
    kernelVerify->writeArgument(8, counterTest);
    //theKernel->writeToBuffer(0, &theLength, sizeof(uint));
    //std::cout << "DEBUG: Setting arguments ... " << std::endl;
    //std::cout << "DEBUG: arguments set, enqueueing kernel... " << std::endl;
    cl_int ret = clEnqueueNDRangeKernel(
      theGPU.commandQueue,
      kernelVerify->kernel,
      1,
      NULL,
      kernelVerify->global_item_size,
      kernelVerify->local_item_size,
      0,
      NULL,
      NULL
    );
    if (ret != CL_SUCCESS) {
      theTestLogger << "Failed to enqueue kernel. Return code: " << ret << ". " << Logger::endL;
      return false;
    }
    //std::cout << "DEBUG: kernel enqueued, proceeding to read buffer. " << std::endl;
    if (counterTest % 100 == 0) {
      auto timeCurrent = std::chrono::system_clock::now();
      std::chrono::duration<double> elapsed_seconds = timeCurrent - timeStart;
      std::cout << "Scheduled " << counterTest << " 32-byte messages in " << elapsed_seconds.count() << " second(s),"
      << " current speed: "
      << ((counterTest + 1) / elapsed_seconds.count()) << " public keys per second." << std::endl;
    }
  }
  theTestLogger << "Fetching public keys... " << Logger::endL;
  cl_mem& resultVerifications = kernelVerify->getOutput(0)->theMemory;
  cl_int ret = clEnqueueReadBuffer (
    theGPU.commandQueue,
    resultVerifications,
    CL_TRUE,
    0,
    this->outputVerifications.size(),
    &this->outputVerifications[0],
    0,
    NULL,
    NULL
  );
  if (ret != CL_SUCCESS) {
    theTestLogger << "Failed to enqueue read buffer. Return code: " << ret << ". " << Logger::endL;
    return false;
  }
  auto timeCurrent = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed_seconds = timeCurrent - timeStart;
  theTestLogger << "Generated " << counterTest << " public keys in " << elapsed_seconds.count() << " second(s). "
  << Logger::endL;
  theTestLogger << "Speed: "
  << (this->numMessagesPerPipeline / elapsed_seconds.count()) << " signature(s) per second." << Logger::endL;
  bool isGood = true;
  for (counterTest = 0; counterTest < this->numMessagesPerPipeline; counterTest ++) {
    if (this->outputVerifications[counterTest] == 0 && !tamperWithSignature) {
      theTestLogger << Logger::colorRed << "ERROR: Failed to verify signature " << counterTest << ". " << Logger::endL;
      isGood = false;
      break;
    }
    if (this->outputVerifications[counterTest] == 1 && tamperWithSignature) {
      theTestLogger << Logger::colorRed << "ERROR: wrongly verified tampered with signature "
      << counterTest << ". " << Logger::endL;
      isGood = false;
      break;
    }
    if (this->outputVerifications[counterTest] != 1 && this->outputVerifications[counterTest] != 0) {
      theTestLogger << Logger::colorRed << "ERROR: signature verification of signature " << counterTest << " has value: "
      << this->outputVerifications[counterTest] << ". " << Logger::endL;
      isGood = false;
      break;
    }
  }
  if (!isGood) {
    theTestLogger << "FATAL ERROR. " << Logger::endL;
  }
  return true;
}
