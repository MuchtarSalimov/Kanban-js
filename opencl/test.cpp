#include "logging.h"
#include <sstream>
#include <iomanip>
#include "gpu.h"
#include "cl/secp256k1_cpp.h"


Logger logTest("../logfiles/logTest.txt", "[test] ");
extern Logger logServer;


secp256k1_callback criticalFailure;

void criticalFailureHandler(const char *text, void* data){
  (void) data;
  logTest << text;
}


const unsigned int GPUMemoryAvailable = 10000000; //for the time being, this should be equal to defaultBufferSize from gpu.cpp
unsigned char bufferGPUMemory[GPUMemoryAvailable];

bool testGPU(secp256k1_scalar& signatureR, secp256k1_scalar& signatureS, secp256k1_ge& publicKey, secp256k1_scalar& message) {
  GPU theGPU;
  if (!theGPU.initializeKernels())
    return false;
  secp256k1_ecmult_context multiplicationContext;
  secp256k1_ecmult_context_init(&multiplicationContext);
  secp256k1_ecmult_context_build(&multiplicationContext, &criticalFailure);


  std::shared_ptr<GPUKernel> theKernel = theGPU.theKernels[GPU::kernelVerifySignature];

  theKernel->writeToBuffer(1, &signatureR, sizeof(multiplicationContext));
  theKernel->writeToBuffer(2, &signatureR, sizeof(signatureR));
  theKernel->writeToBuffer(3, &signatureS, sizeof(signatureS));
  theKernel->writeToBuffer(4, &publicKey, sizeof(publicKey));
  theKernel->writeToBuffer(5, &message, sizeof(message));

  cl_int ret = clEnqueueNDRangeKernel(
    theGPU.commandQueue, theKernel->kernel, 1, NULL,
    &theKernel->global_item_size, &theKernel->local_item_size, 0, NULL, NULL
  );
  if (ret != CL_SUCCESS) {
    logServer << "Failed to enqueue kernel. Return code: " << ret << ". ";
    return false;
  }
  cl_mem& result = theKernel->outputs[0]->theMemory;
  unsigned char resultChar[1];
  ret = clEnqueueReadBuffer(theGPU.commandQueue, result, CL_TRUE, 0, 1, &resultChar, 0, NULL, NULL);
  if (ret != CL_SUCCESS) {
    logServer << "Failed to read buffer. Return code: " << ret << Logger::endL;
    return false;
  }
  logServer << "Return of GPU verification: " << (int) resultChar[0] << Logger::endL;
  return true;
}

int mainTest() {
  criticalFailure.data = 0;
  criticalFailure.fn = criticalFailureHandler;
  secp256k1_ecmult_context multiplicationContext;
  secp256k1_ecmult_context_init(&multiplicationContext);
  secp256k1_ecmult_context_build(&multiplicationContext, &criticalFailure);
  logTest << "DEBUG: multiplicationContext:\n" << toStringSecp256k1_MultiplicationContext(multiplicationContext) << Logger::endL;
  secp256k1_ecmult_gen_context generatorContext;
  secp256k1_ecmult_gen_context_init(&generatorContext);
  secp256k1_ecmult_gen_context_build(&generatorContext, &criticalFailure);
  logTest << "\n**********\n"
          << "DEBUG: generatorContext: " << toStringSecp256k1_GeneratorContext(generatorContext) << Logger::endL;
  secp256k1_scalar signatureS, signatureR;
  secp256k1_scalar secretKey = SECP256K1_SCALAR_CONST(
    13, 17, 19, 0, 0, 0, 0, 0
  );
  secp256k1_scalar message = SECP256K1_SCALAR_CONST(
    2, 3, 5, 7, 11, 13, 17, 19
  );
  secp256k1_scalar nonce = SECP256K1_SCALAR_CONST(
    3, 5, 7, 11, 13, 17, 19, 23
  );
  secp256k1_ge publicKey;
  secp256k1_gej publicKeyJacobianCoordinates;

  secp256k1_ecmult_gen(&generatorContext, &publicKeyJacobianCoordinates, &secretKey);

  secp256k1_ge_set_gej(&publicKey, &publicKeyJacobianCoordinates);
  logTest << "DEBUG: public key: " << toStringSecp256k1_ECPoint(publicKey) << Logger::endL;

  int recId = 5;

  logTest << "Got to here pt 5. " << Logger::endL;
  secp256k1_ecdsa_sig_sign(&generatorContext, &signatureR, &signatureS, &secretKey, &message, &nonce, &recId);
  logTest << "SigR: " << toStringSecp256k1_Scalar(signatureR) << Logger::endL;
  logTest << "SigS: " << toStringSecp256k1_Scalar(signatureS) << Logger::endL;

  int signatureResult = secp256k1_ecdsa_sig_verify(&multiplicationContext, &signatureR, &signatureS, &publicKey, &message);
  logTest << "DEBUG: signature verification: " << signatureResult << Logger::endL;

  logTest << "Got to here pt 6. " << Logger::endL;
  logTest << "secret: " << toStringSecp256k1_Scalar(secretKey) << Logger::endL;
  logTest << "message: " << toStringSecp256k1_Scalar(message) << Logger::endL;
  logTest << "nonce: " << toStringSecp256k1_Scalar(nonce) << Logger::endL;
  logTest << "outputR: " << toStringSecp256k1_Scalar(signatureR) << Logger::endL;
  logTest << "outputS: " << toStringSecp256k1_Scalar(signatureS) << Logger::endL;
  if (!testGPU(signatureR, signatureS, publicKey, message))
    return -1;
  return 0;
}
