// 1999-05-07
// bkoz 

// Copyright (C) 1999, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// 23.2.4.2 vector capacity

#include <vector>
#include <stdexcept>
#include <testsuite_allocator.h>
#include <testsuite_hooks.h>

// libstdc++/8230
void test02()
{
  bool test __attribute__((unused)) = true;
  {
    std::vector<int>  array;
    const std::size_t size = array.max_size();
    try 
      {
	array.reserve(size);
      } 
    catch (const std::length_error& error) 
      {
	test &= false;
      }
    catch (const std::bad_alloc& error)
      {
	test &= true;
      }
    catch (...)
      {
	test &= false;
      }
    VERIFY( test );
  }

  {
    std::vector<int>  array;
    const std::size_t size = array.max_size() + 1;
    try 
      {
	array.reserve(size);
      } 
    catch (const std::length_error& error) 
      {
	test &= true;
      }
    catch (...)
      {
	test &= false;
      }
    VERIFY( test );
  }
}

int main()
{
  test02();
  return 0;
}
