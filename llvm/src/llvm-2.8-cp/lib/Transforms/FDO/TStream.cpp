//===- TStream.cpp - Multi-ouput raw_ostream --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/FDO/TStream.h"


using namespace llvm;


TStream::TStream(unsigned p, bool override)
  : initV(p), V(p)
{
  if(override)
    addStream(&errs(), p);
  else
    addStream(&errs(), vl::warn);
}


TStream::TStream(llvm::raw_ostream* s, unsigned vl) : 
  initV(vl), V(vl)
{
  addStream(&errs(), vl::warn);
  addStream(s, vl);
}

bool TStream::addStream(llvm::raw_ostream* s, unsigned vl)
{
  if(s == NULL) return(false);
  
  streams.push_back(std::make_pair(s,vl));
  return(true);
}

void TStream::setDefaultPriority(unsigned p)
{
  if(p <= vl::never) p = vl::never;
  if(p > vl::always) p = vl::always;
  initV = p;
}

void TStream::flush()
{
  for(unsigned i = 0, E = streams.size(); i < E; i++)
    streams[i].first->flush();
}


TStream& TStream::operator<<(char C)
{
  for(unsigned i = 0, E = streams.size(); i < E; i++)
    if(streams[i].second <= V)
      (*(streams[i].first)) << C;
  return(*this);
}

TStream& TStream::operator<<(unsigned char C)
{
  for(unsigned i = 0, E = streams.size(); i < E; i++)
    if(streams[i].second <= V)
      (*(streams[i].first)) << C;
  return(*this);
}

TStream& TStream::operator<<(unsigned long N)
{
  for(unsigned i = 0, E = streams.size(); i < E; i++)
    if(streams[i].second <= V)
      (*(streams[i].first)) << N;
  return(*this);
}

TStream& TStream::operator<<(long N)
{
  for(unsigned i = 0, E = streams.size(); i < E; i++)
    if(streams[i].second <= V)
      (*(streams[i].first)) << N;
  return(*this);
}

TStream& TStream::operator<<(unsigned long long N)
{
  for(unsigned i = 0, E = streams.size(); i < E; i++)
    if(streams[i].second <= V)
      (*(streams[i].first)) << N;
  return(*this);
}

TStream& TStream::operator<<(long long N)
{
  for(unsigned i = 0, E = streams.size(); i < E; i++)
    if(streams[i].second <= V)
      (*(streams[i].first)) << N;
  return(*this);
}

TStream& TStream::operator<<(const void* P)
{
  for(unsigned i = 0, E = streams.size(); i < E; i++)
    if(streams[i].second <= V)
      (*(streams[i].first)) << P;
  return(*this);
}

TStream& TStream::operator<<(unsigned int N)
{
  for(unsigned i = 0, E = streams.size(); i < E; i++)
    if(streams[i].second <= V)
      (*(streams[i].first)) << N;
  return(*this);
}

TStream& TStream::operator<<(int N)
{
  for(unsigned i = 0, E = streams.size(); i < E; i++)
    if(streams[i].second <= V)
      (*(streams[i].first)) << N;
  return(*this);
}

TStream& TStream::operator<<(double N)
{
  for(unsigned i = 0, E = streams.size(); i < E; i++)
    if(streams[i].second <= V)
      (*(streams[i].first)) << N;
  return(*this);
}

TStream& TStream::operator<<(const char* Str)
{
  for(unsigned i = 0, E = streams.size(); i < E; i++)
    if(streams[i].second <= V)
      (*(streams[i].first)) << Str;
  return(*this);
}

TStream& TStream::operator<<(const std::string& Str)
{
  for(unsigned i = 0, E = streams.size(); i < E; i++)
    if(streams[i].second <= V)
      (*(streams[i].first)) << Str;
  return(*this);
}

TStream& TStream::operator<<(const format_object_base& Fmt)
{
  for(unsigned i = 0, E = streams.size(); i < E; i++)
    if(streams[i].second <= V)
      (*(streams[i].first)) << Fmt;
  return(*this);
}

// innefficient, but dead simple...
TStream& TStream::indent(unsigned n)
{
  for(unsigned i = 0, E = streams.size(); i < E; i++)
    if(streams[i].second <= V)
      for(unsigned s = 0; s < n; s++)
        (*(streams[i].first)) << " ";
  return(*this);
}
