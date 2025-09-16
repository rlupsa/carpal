#pragma once

#include "carpal/Future.h"
#include "carpal/StreamSource.h"
#include "TestHelperStreamReader.h"
#include "Generator.h"

#include <stdint.h>
#include <string>

// parses the string and returns the sum of integers in it (the most direct parsing)
uint64_t sumIntegersDirect(std::string const& content);

// gets characters from the reader, parses them into integers and sums them
uint64_t sumIntegersThroughReader(std::shared_ptr<TestHelperStreamReader> pReader);

// gets characters from the reader trhough a queue, parses them into integers and sums them
uint64_t sumIntegersThroughQueuePull(std::shared_ptr<TestHelperStreamReader> pReader);

// regular generator returning the characters
Generator<char,EofMark> textCharGenerator(std::string const* pString);

// parses and sums the integers from the character generator
uint64_t sumIntegersThroughGenerator(Generator<char,EofMark>&& generator);

// parses and returns the integers from the regular character generator as an integer regular generator
Generator<uint64_t,EofMark> integersParserThroughGenerator(Generator<char,EofMark>&& generator);

// sums the integers from the integer regular generator
uint64_t sumIntegersThroughIntGenerator(Generator<uint64_t,EofMark>&& generator);

// async generator returning the characters
carpal::StreamSource<char> bufferedTextRead(std::shared_ptr<TestHelperStreamReader> pReader);


carpal::Future<uint64_t> sumIntegersTextReader(carpal::StreamSource<char> src);
uint64_t sumIntegersThroughFunctionGenerator(Generator<std::function<char()>,EofMark>&& generator);


