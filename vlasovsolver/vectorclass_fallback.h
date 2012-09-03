#ifndef VECTORCLASS_PORTABLE_H
#define VECTORCLASS_PORTABLE_H

/*
  This file is part of Vlasiator.

  Copyright 2010, 2011, 2012 Finnish Meteorological Institute

  Vlasiator is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 3
  as published by the Free Software Foundation.

  Vlasiator is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

/*! \file vectorclass_fallback.h
  \brief Simple class for implementing a vector with 4 real values

*/




template <class T>
class Vec4Real {
public:
   T val[4];
   
   // donot initi v
   Vec4Real() { }
   // Replicate scalar x across v.
   Vec4Real(T x){
      for(unsigned int i=0;i<4;i++)
         val[i]=x;
   }
   
   // Replicate 4 values across v.   
   Vec4Real(T a,T b,T c,T d){
      val[0]=a;
      val[1]=b;
      val[2]=c;
      val[3]=d;
      
   }
   // Copy vector v.
   Vec4Real(Vec4Real const &x){
      for(unsigned int i=0;i<4;i++)
         val[i]=x.val[i];
   }

   // Member function to load from array (unaligned)
   Vec4Real & load(T const * p) {
     for(unsigned int i=0;i<4;i++)
        val[i]=p[i];
 
      return *this;
   }
   // Member function to load from array, aligned by 32
   Vec4Real & load_a(T const * p) {
      return this->load(p);
   }
   
   Vec4Real & insert(int i,T const &x) {
      val[i]=x;
      return *this;
   }


// Member function to store into array (unaligned)
   void store(T * p) const {
     for(unsigned int i=0;i<4;i++)
        p[i]=val[i];
   }
   // Member function to store into array, aligned by 32
   void store_a(T * p) const {
      this->store(p);
   }


   Vec4Real & operator = (Vec4Real const & r){
      for(unsigned int i=0;i<4;i++)
         val[i]=r.val[i];
      
      return *this;
   }   
};

template <class T>
static inline Vec4Real<T> abs(const Vec4Real<T> &l){
   return Vec4Real<T>(fabs(l.val[0]),
                    fabs(l.val[1]),
                    fabs(l.val[2]),
                    fabs(l.val[3]));
}

template <class T>
static inline Vec4Real<T> operator + (const Vec4Real<T> &l, const Vec4Real<T> &r){
   return Vec4Real<T>(l.val[0]+r.val[0],
                      l.val[1]+r.val[1],
                      l.val[2]+r.val[2],
                      l.val[3]+r.val[3]);
}

template <class T>
static inline Vec4Real<T> operator + (const T &l, const Vec4Real<T> &r){
   return Vec4Real<T>(l+r.val[0],
                      l+r.val[1],
                      l+r.val[2],
                      l+r.val[3]);
}

template <class T>
static inline Vec4Real<T> operator + (const Vec4Real<T> &l, const T &r){
   return Vec4Real<T>(l.val[0]+r,
                      l.val[1]+r,
                      l.val[2]+r,
                      l.val[3]+r);
}

template <class T>
static inline Vec4Real<T> operator - (const Vec4Real<T> &l, const Vec4Real<T> &r)
{
   return Vec4Real<T>(l.val[0]-r.val[0],
                    l.val[1]-r.val[1],
                    l.val[2]-r.val[2],
                    l.val[3]-r.val[3]);
}

template <class T>
static inline Vec4Real<T> operator - (const T &l, const Vec4Real<T> &r){
   return Vec4Real<T>(l-r.val[0],
                      l-r.val[1],
                      l-r.val[2],
                      l-r.val[3]);
}

template <class T>
static inline Vec4Real<T> operator - (const Vec4Real<T> &l, const T &r){
   return Vec4Real<T>(l.val[0]-r,
                      l.val[1]-r,
                      l.val[2]-r,
                      l.val[3]-r);
}

template <class T>
static inline Vec4Real<T> operator * (const Vec4Real<T> &l, const Vec4Real<T> &r)
{
   return Vec4Real<T>(l.val[0]*r.val[0],
                    l.val[1]*r.val[1],
                    l.val[2]*r.val[2],
                    l.val[3]*r.val[3]);
}



template <class T>
static inline Vec4Real<T> operator * (const Vec4Real<T> &l, const T &r)
{
   return Vec4Real<T>(l.val[0]*r,
                    l.val[1]*r,
                    l.val[2]*r,
                    l.val[3]*r);
}

template <class T>
static inline Vec4Real<T> operator * (const T &l,const Vec4Real<T> &r)
{
   return Vec4Real<T>(r.val[0]*l,
                      r.val[1]*l,
                      r.val[2]*l,
                      r.val[3]*l);
}

template <class T>
static inline Vec4Real<T> operator / (const Vec4Real<T> &l, const Vec4Real<T> &r)
{
   return Vec4Real<T>(l.val[0]/r.val[0],
                    l.val[1]/r.val[1],
                    l.val[2]/r.val[2],
                    l.val[3]/r.val[3]);
}

template <class T>
static inline  Vec4Real<T> & operator += (Vec4Real<T> &l, const Vec4Real<T> &r){
   l=l+r;
   return l;
}
   
template <class T>   
static inline Vec4Real<T> & operator -= (Vec4Real<T> &l, const Vec4Real<T> &r){
   l=l-r;
   return l;
}

template <class T>
static inline Vec4Real<T> min(Vec4Real<T> const & l, Vec4Real<T> const & r){
   return Vec4Real<T>(l.val[0]<r.val[0]?l.val[0]:r.val[0],
                    l.val[1]<r.val[1]?l.val[1]:r.val[1],
                    l.val[2]<r.val[2]?l.val[2]:r.val[2],
                    l.val[3]<r.val[3]?l.val[3]:r.val[3]);
}

template <class T>
static inline Vec4Real<T> max(Vec4Real<T> const & l, Vec4Real<T> const & r){
   return Vec4Real<T>(l.val[0]>r.val[0]?l.val[0]:r.val[0],
                    l.val[1]>r.val[1]?l.val[1]:r.val[1],
                    l.val[2]>r.val[2]?l.val[2]:r.val[2],
                    l.val[3]>r.val[3]?l.val[3]:r.val[3]);
}












#endif
