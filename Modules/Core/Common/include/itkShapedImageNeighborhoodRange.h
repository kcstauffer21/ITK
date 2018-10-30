/*=========================================================================
*
*  Copyright Insight Software Consortium
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*         http://www.apache.org/licenses/LICENSE-2.0.txt
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
*
*=========================================================================*/

#ifndef itkShapedImageNeighborhoodRange_h
#define itkShapedImageNeighborhoodRange_h

#include <algorithm> // For copy_n.
#include <cassert>
#include <cstddef> // For ptrdiff_t.
#include <iterator> // For random_access_iterator_tag.
#include <limits>
#include <type_traits> // For conditional and is_const.

#include "itkIndex.h"
#include "itkSize.h"
#include "itkZeroFluxNeumannImageNeighborhoodPixelAccessPolicy.h"

namespace itk
{
namespace Experimental
{

/**
 * \class ShapedImageNeighborhoodRange
 * Modern C++11 range to iterate over a neighborhood of pixels.
 * Designed to conform to Standard C++ Iterator requirements,
 * so that it can be used in range-based for loop, and passed to
 * Standard C++ algorithms.
 *
 * The following example creates a 3 x 5 neighborhood around pixel [10, 20]
 * and adds 42 to each neighborhood pixel, using a range-based for loop:
   \code
   const ImageType::IndexType location = {10, 20};
   const itk::Size<ImageType::ImageDimension> radius = { { 1, 2 } };
   const std::vector<OffsetType> offsets = itk::GenerateRectangularImageNeighborhoodOffsets(radius)
   itk::ShapedImageNeighborhoodRange<ImageType> neighborhoodRange{ *image, location, offsets };

   for (auto&& neighborhoodPixel : neighborhoodRange)
   {
     neighborhoodPixel = neighborhoodPixel + 42;
   }
   \endcode
 *
 * The following example prints the values of the neighborhood pixels:
   \code
   for (const PixelType neighborhoodPixel : neighborhoodRange)
   {
     std::cout << neighborhoodPixel << std::endl;
   }
   \endcode
 *
 * The inner product of the neighborhood with a kernel can be produced with
 * std::inner_product (from the Standard C++  header "numeric"), as follows:
   \code
   double result = std::inner_product(
     kernel.begin(),
     kernel.end(),
     neighborhoodRange.begin(),
     0.0);
   \endcode
 *
 * \note Strictly speaking, the itk::ShapedImageNeighborhoodRange iterator classes do not
 * fully comply with the C++11 random access iterator requirements, because
 * their operator*() returns a proxy to the pixel, instead of a reference.
 * Which implies that ShapedImageNeighborhoodRange iterators are not guaranteed to work
 * well as argument to a C++ Standard Library function that requires a
 * standard compliant iterator. However, this "pixel proxy" very much behaves like a
 * reference to the pixel, and in practice, passing such an iterator to an std function
 * usually just works!
 *
 * \author Niels Dekker, LKEB, Leiden University Medical Center
 *
 * \see ShapedNeighborhoodIterator
 * \ingroup ImageIterators
 * \ingroup ITKCommon
 */
template<typename TImage,
  typename TImageNeighborhoodPixelAccessPolicy = ZeroFluxNeumannImageNeighborhoodPixelAccessPolicy<TImage>>
class ShapedImageNeighborhoodRange final
{
private:
  using ImageType = TImage;
  using ImageDimensionType = typename TImage::ImageDimensionType;
  using ImageSizeType = typename TImage::SizeType;
  using ImageSizeValueType = typename TImage::SizeValueType;
  using PixelType = typename TImage::PixelType;
  using InternalPixelType = typename TImage::InternalPixelType;
  using NeighborhoodAccessorFunctorType = typename TImage::NeighborhoodAccessorFunctorType;
  static constexpr ImageDimensionType ImageDimension = TImage::ImageDimension;
  using IndexType = typename TImage::IndexType;
  using IndexValueType = typename TImage::IndexValueType;
  using OffsetType = Offset<ImageDimension>;

  // PixelProxy: internal class that aims to act like a reference to a pixel:
  // It acts either like 'PixelType &' or like 'const PixelType &', depending
  // on its boolean template argument, VIsConst.
  // The proxy retrieves the pixel value using a ImageNeighborhoodPixelAccessPolicy.
  // Note: the extra TDummy argument aims to fix AppleClang 6.0.0.6000056 error
  // "explicit specialization of 'PixelProxy'"and GCC 5.4.0 error "explicit
  // specialization in non-namespace scope".
  template <bool VIsConst, typename TDummy = void> class PixelProxy {};

  // PixelProxy specialization for const pixel types:
  // acts like 'const PixelType &'
  template <typename TDummy>
  class PixelProxy<true, TDummy> final
  {
  private:
    // Pointer to the buffer of the image. Should not be null.
    const InternalPixelType* const m_ImageBufferPointer;

    // Pixel access policy.
    const TImageNeighborhoodPixelAccessPolicy m_PixelAccessPolicy;

  public:
    // Deleted member functions:
    PixelProxy() = delete;
    PixelProxy& operator=(const PixelProxy&) = delete;

    // Explicitly-defaulted member functions:
    PixelProxy(const PixelProxy&) ITK_NOEXCEPT = default;
    ~PixelProxy() = default;

    // Constructor, called directly by operator*() of the iterator class.
    PixelProxy(
      const InternalPixelType* const imageBufferPointer,
      const ImageSizeType& imageSize,
      const OffsetType& offsetTable,
      const NeighborhoodAccessorFunctorType& neighborhoodAccessor,
      const IndexType& pixelIndex) ITK_NOEXCEPT
      :
    m_ImageBufferPointer{imageBufferPointer},
    m_PixelAccessPolicy{ imageSize, offsetTable, neighborhoodAccessor, pixelIndex }
    {
    }

    // Allows implicit conversion from non-const to const proxy.
    PixelProxy(const PixelProxy<false>& pixelProxy) ITK_NOEXCEPT
      :
    m_ImageBufferPointer{ pixelProxy.m_ImageBufferPointer },
    m_PixelAccessPolicy{ pixelProxy.m_PixelAccessPolicy}
    {
    }

    // Conversion operator.
    operator PixelType() const ITK_NOEXCEPT
    {
      return m_PixelAccessPolicy.GetPixelValue(m_ImageBufferPointer);
    }
  };


  // PixelProxy specialization for non-const pixel types:
  // acts like 'PixelType &'.
  template <typename TDummy>
  class PixelProxy<false, TDummy> final
  {
  private:
    // The const proxy is a friend, to ease implementing conversion from
    // a non-const proxy to a const proxy.
    friend class PixelProxy<true>;

    // Pointer to the buffer of the image. Should not be null.
    InternalPixelType* const m_ImageBufferPointer;

    // Pixel access policy.
    const TImageNeighborhoodPixelAccessPolicy m_PixelAccessPolicy;

  public:
    // Deleted member functions:
    PixelProxy() = delete;

    // Explicitly-defaulted member functions:
    ~PixelProxy() = default;
    PixelProxy(const PixelProxy&) ITK_NOEXCEPT = default;

    // Constructor, called directly by operator*() of the iterator class.
    PixelProxy(
      InternalPixelType* const imageBufferPointer,
      const ImageSizeType& imageSize,
      const OffsetType& offsetTable,
      const NeighborhoodAccessorFunctorType& neighborhoodAccessor,
      const IndexType& pixelIndex) ITK_NOEXCEPT
      :
    m_ImageBufferPointer{imageBufferPointer},
    m_PixelAccessPolicy{ imageSize, offsetTable, neighborhoodAccessor, pixelIndex }
    {
    }

    // Conversion operator.
    operator PixelType() const ITK_NOEXCEPT
    {
      return m_PixelAccessPolicy.GetPixelValue(m_ImageBufferPointer);
    }

    // Operator to assign a pixel value to the proxy.
    PixelProxy& operator=(const PixelType& pixelValue) ITK_NOEXCEPT
    {
      m_PixelAccessPolicy.SetPixelValue(m_ImageBufferPointer, pixelValue);
      return *this;
    }

    // Copy-assignment operator.
    PixelProxy& operator=(const PixelProxy& pixelProxy) ITK_NOEXCEPT
    {
      // Note that this assignment operator only copies the pixel value.
      // That is the normal behavior when a reference is assigned to another.
      const PixelType pixelValue = pixelProxy;
      *this = pixelValue;
      return *this;
    }


    friend void swap(PixelProxy lhs, PixelProxy rhs) ITK_NOEXCEPT
    {
      const auto lhsPixelValue = lhs.m_PixelAccessPolicy.GetPixelValue(lhs.m_ImageBufferPointer);
      const auto rhsPixelValue = rhs.m_PixelAccessPolicy.GetPixelValue(rhs.m_ImageBufferPointer);

      // Swap only the pixel values, not the image buffer pointers!
      lhs.m_PixelAccessPolicy.SetPixelValue(lhs.m_ImageBufferPointer, rhsPixelValue);
      rhs.m_PixelAccessPolicy.SetPixelValue(rhs.m_ImageBufferPointer, lhsPixelValue);
    }
  };


  /**
   * \class QualifiedIterator
   * Iterator class that is either 'const' or non-const qualified.
   * A non-const qualified instantiation of this template allows the pixel that
   * it points to, to be modified. A const qualified instantiation does not.
   *
   * \note The definition of this class is private. Please use its type alias
   * ShapedImageNeighborhoodRange::iterator, or ShapedImageNeighborhoodRange::const_iterator!
   * \see ShapedImageNeighborhoodRange
   * \ingroup ImageIterators
   * \ingroup ITKCommon
   */
  template <bool VIsConst>
  class QualifiedIterator final
  {
  private:
    // Const and non-const iterators are friends, in order to implement the
    // constructor that allow conversion from non-const to const iterator.
    friend class QualifiedIterator<!VIsConst>;

    // ShapedImageNeighborhoodRange is a friend, as it should be the only one that can
    // directly use the private constructor of the iterator.
    friend class ShapedImageNeighborhoodRange;

    // Image type class that is either 'const' or non-const qualified, depending on QualifiedIterator and TImage.
    using QualifiedImageType = typename std::conditional<VIsConst, const ImageType, ImageType>::type;

    static constexpr bool IsImageTypeConst = std::is_const<QualifiedImageType>::value;

    using QualifiedInternalPixelType = typename std::conditional<IsImageTypeConst, const InternalPixelType, InternalPixelType>::type;

    // Pixel type class that is either 'const' or non-const qualified, depending on QualifiedImageType.
    using QualifiedPixelType = typename std::conditional<IsImageTypeConst, const PixelType, PixelType>::type;

    // Pointer to the buffer of the image. Only null when the iterator is default-constructed.
    QualifiedInternalPixelType* m_ImageBufferPointer = nullptr;

    // Image size.
    ImageSizeType m_ImageSize = { {} };

    // A copy of the offset table of the image.
    OffsetType m_OffsetTable = { {} };

    // The accessor of the image.
    NeighborhoodAccessorFunctorType m_NeighborhoodAccessor;

    // The pixel coordinates of the location of the neighborhood.
    // May be outside the image!
    IndexType m_Location = { {} };

    const OffsetType* m_CurrentOffset = nullptr;

    // Private constructor, used to create the begin and the end iterator of a range.
    // Only used by its friend class ShapedImageNeighborhoodRange.
    QualifiedIterator(
      QualifiedInternalPixelType* const imageBufferPointer,
      const ImageSizeType& imageSize,
      const OffsetType& offsetTable,
      const NeighborhoodAccessorFunctorType& neighborhoodAccessor,
      const IndexType& location,
      const OffsetType* const offset) ITK_NOEXCEPT
      :
    m_ImageBufferPointer{ imageBufferPointer },
    // Note: Use parentheses instead of curly braces to initialize data members,
    // to avoid AppleClang 6.0.0.6000056 compilation error, "no viable conversion..."
    m_ImageSize(imageSize),
    m_OffsetTable(offsetTable),
    m_NeighborhoodAccessor(neighborhoodAccessor),
    m_Location(location),
    m_CurrentOffset{offset}
    {
      assert(m_ImageBufferPointer != nullptr);
    }

  public:
    // Types conforming the iterator requirements of the C++ standard library:
    using difference_type = std::ptrdiff_t;
    using value_type = PixelType;
    using reference = PixelProxy<IsImageTypeConst>;
    using pointer = QualifiedPixelType*;
    using iterator_category = std::random_access_iterator_tag;


    /** Default-constructor, as required for any C++11 Forward Iterator. Offers
     * the guarantee added to the C++14 Standard: "value-initialized iterators
     * may be compared and shall compare equal to other value-initialized
     * iterators of the same type."
     */
    QualifiedIterator() = default;

    /** Constructor that allows implicit conversion from non-const to const
     * iterator. Also serves as copy-constructor of a non-const iterator.  */
    QualifiedIterator(const QualifiedIterator<false>& arg) ITK_NOEXCEPT
      :
      m_ImageBufferPointer{ arg.m_ImageBufferPointer },
      // Note: Use parentheses instead of curly braces to initialize data members,
      // to avoid AppleClang 6.0.0.6000056 compilation error, "no viable conversion..."
      m_ImageSize(arg.m_ImageSize),
      m_OffsetTable(arg.m_OffsetTable),
      m_NeighborhoodAccessor(arg.m_NeighborhoodAccessor),
      m_Location(arg.m_Location),
      m_CurrentOffset{arg.m_CurrentOffset}
    {
    }


    /**  Returns a reference to the current pixel. */
    reference operator*() const ITK_NOEXCEPT
    {
      return reference(m_ImageBufferPointer, m_ImageSize, m_OffsetTable, m_NeighborhoodAccessor, m_Location + *m_CurrentOffset);
    }


    /** Prefix increment ('++it'). */
    QualifiedIterator& operator++() ITK_NOEXCEPT
    {
      assert(m_CurrentOffset != nullptr);
      ++m_CurrentOffset;
      return *this;
    }


    /** Postfix increment ('it++').
     * \note Usually prefix increment ('++it') is preferable. */
    QualifiedIterator operator++(int) ITK_NOEXCEPT
    {
      auto result = *this;
      ++(*this);
      return result;
    }


    /** Prefix decrement ('--it'). */
    QualifiedIterator& operator--() ITK_NOEXCEPT
    {
      assert(m_CurrentOffset != nullptr);
      --m_CurrentOffset;
      return *this;
    }


    /** Postfix increment ('it--').
     * \note  Usually prefix increment ('--it') is preferable. */
    QualifiedIterator operator--(int) ITK_NOEXCEPT
    {
      auto result = *this;
      --(*this);
      return result;
    }


    /** Returns (it1 == it2) for iterators it1 and it2. Note that these iterators
     * should be from the same range. This operator does not support comparing iterators
     * from different ranges. */
    friend bool operator==(const QualifiedIterator& lhs, const QualifiedIterator& rhs) ITK_NOEXCEPT
    {
      assert(lhs.m_ImageBufferPointer == rhs.m_ImageBufferPointer);
      assert(lhs.m_ImageSize == rhs.m_ImageSize);
      assert(lhs.m_OffsetTable == rhs.m_OffsetTable);

      return lhs.m_CurrentOffset == rhs.m_CurrentOffset;
    }


    /** Returns (it1 != it2) for iterators it1 and it2. */
    friend bool operator!=(const QualifiedIterator& lhs, const QualifiedIterator& rhs) ITK_NOEXCEPT
    {
      // Implemented just like the corresponding std::rel_ops operator.
      return !(lhs == rhs);
    }


    /** Returns (it1 < it2) for iterators it1 and it2. */
    friend bool operator<(const QualifiedIterator& lhs, const QualifiedIterator& rhs) ITK_NOEXCEPT
    {
      assert(lhs.m_ImageBufferPointer == rhs.m_ImageBufferPointer);
      assert(lhs.m_ImageSize == rhs.m_ImageSize);
      assert(lhs.m_OffsetTable == rhs.m_OffsetTable);

      return lhs.m_CurrentOffset < rhs.m_CurrentOffset;
    }


    /** Returns (it1 > it2) for iterators it1 and it2. */
    friend bool operator>(const QualifiedIterator& lhs, const QualifiedIterator& rhs) ITK_NOEXCEPT
    {
      // Implemented just like the corresponding std::rel_ops operator.
      return rhs < lhs;
    }


    /** Returns (it1 <= it2) for iterators it1 and it2. */
    friend bool operator<=(const QualifiedIterator& lhs, const QualifiedIterator& rhs) ITK_NOEXCEPT
    {
      // Implemented just like the corresponding std::rel_ops operator.
      return !(rhs < lhs);
    }


    /** Returns (it1 >= it2) for iterators it1 and it2. */
    friend bool operator>=(const QualifiedIterator& lhs, const QualifiedIterator& rhs) ITK_NOEXCEPT
    {
      // Implemented just like the corresponding std::rel_ops operator.
      return !(lhs < rhs);
    }


    /** Does (it += d) for iterator 'it' and integer value 'n'. */
    friend QualifiedIterator& operator+=(QualifiedIterator& it, const difference_type n) ITK_NOEXCEPT
    {
      it.m_CurrentOffset += n;
      return it;
    }

    /** Does (it -= d) for iterator 'it' and integer value 'n'. */
    friend QualifiedIterator& operator-=(QualifiedIterator& it, const difference_type n) ITK_NOEXCEPT
    {
      it += (-n);
      return it;
    }

    /** Returns (it1 - it2) for iterators it1 and it2. */
    friend difference_type operator-(const QualifiedIterator& lhs, const QualifiedIterator& rhs) ITK_NOEXCEPT
    {
      assert(lhs.m_ImageBufferPointer == rhs.m_ImageBufferPointer);
      assert(lhs.m_ImageSize == rhs.m_ImageSize);
      assert(lhs.m_OffsetTable == rhs.m_OffsetTable);

      return lhs.m_CurrentOffset - rhs.m_CurrentOffset;
    }


    /** Returns (it + n) for iterator 'it' and integer value 'n'. */
    friend QualifiedIterator operator+(QualifiedIterator it, const difference_type n) ITK_NOEXCEPT
    {
      return it += n;
    }


    /** Returns (n + it) for iterator 'it' and integer value 'n'. */
    friend QualifiedIterator operator+(const difference_type n, QualifiedIterator it) ITK_NOEXCEPT
    {
      return it += n;
    }


    /** Returns (it - n) for iterator 'it' and integer value 'n'. */
    friend QualifiedIterator operator-(QualifiedIterator it, const difference_type n) ITK_NOEXCEPT
    {
      return it += (-n);
    }


    /** Returns it[n] for iterator 'it' and integer value 'n'. */
    reference operator[](const difference_type n) const ITK_NOEXCEPT
    {
      return *(*this + n);
    }


    /** Explicitly-defaulted assignment operator. */
    QualifiedIterator& operator=(const QualifiedIterator&) ITK_NOEXCEPT = default;


    /** Explicitly-defaulted destructor. */
    ~QualifiedIterator() = default;
  };

  static constexpr bool IsImageTypeConst = std::is_const<TImage>::value;

  using QualifiedInternalPixelType = typename std::conditional<IsImageTypeConst, const InternalPixelType, InternalPixelType>::type;


  // ShapedImageNeighborhoodRange data members (strictly private):

  // Pointer to the buffer of the image. Should not be null.
  QualifiedInternalPixelType* m_ImageBufferPointer;

  // Image size.
  ImageSizeType m_ImageSize;

  // A copy of the offset table of the image.
  OffsetType m_OffsetTable;

  NeighborhoodAccessorFunctorType m_NeighborhoodAccessor;

  // Index (pixel coordinates) of the location of the neighborhood relative
  // to the origin of the image. Typically it is the location of the
  // center pixel of the neighborhood. It may be outside the image boundaries.
  IndexType m_Location;

  // The offsets relative to m_Location that specify the neighborhood shape.
  const OffsetType* m_ShapeOffsets;

  // The number of neighborhood pixels.
  const std::size_t m_NumberOfNeighborhoodPixels;

public:
  using const_iterator = QualifiedIterator<true>;
  using iterator = QualifiedIterator<false>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  /** Specifies a range for the neighborhood of a pixel at the specified
   * location. The shape of the neighborhood is specified by a pointer to a
   * contiguous sequence of offsets, relative to the location index.
   */
  ShapedImageNeighborhoodRange(
    ImageType& image,
    const IndexType& location,
    const OffsetType* const shapeOffsets,
    const std::size_t numberOfNeigborhoodPixels)
    :
  m_ImageBufferPointer{image.ImageType::GetBufferPointer()},
  // Note: Use parentheses instead of curly braces to initialize data members,
  // to avoid AppleClang 6.0.0.6000056 compile errors, "no viable conversion..."
  // and "excess elements in struct initializer".
  m_ImageSize(image.GetBufferedRegion().GetSize()),
  m_NeighborhoodAccessor(image.GetNeighborhoodAccessor()),
  m_Location(location),
  m_ShapeOffsets{ shapeOffsets },
  m_NumberOfNeighborhoodPixels{ numberOfNeigborhoodPixels }
  {
    assert(m_ImageBufferPointer != nullptr);
    const OffsetValueType* const offsetTable = image.GetOffsetTable();
    assert(offsetTable != nullptr);

    std::copy_n(offsetTable, ImageDimension, m_OffsetTable.begin());

    m_NeighborhoodAccessor.SetBegin(m_ImageBufferPointer);
  }

  /** Specifies a range for the neighborhood of a pixel at the specified
   * location. The shape of the neighborhood is specified by a container of
   * offsets, relative to the location index. This container of offsets must be
   * a contiguous container, for example std::vector<OffsetType> or
   * std::array<OffsetType>.
   */
  template <typename TContainerOfOffsets>
  ShapedImageNeighborhoodRange(
    ImageType& image,
    const IndexType& location,
    TContainerOfOffsets&& shapeOffsets)
    :
  ShapedImageNeighborhoodRange{
    image,
    location,
    shapeOffsets.data(),
    shapeOffsets.size()}
  {
    static_assert(!std::is_rvalue_reference<decltype(shapeOffsets)>::value,
      "The container of offsets should not be a temporary (rvalue) object!");
  }

  /** Returns an iterator to the first neighborhood pixel. */
  iterator begin() const ITK_NOEXCEPT
  {
    assert(m_ImageBufferPointer != nullptr);
    return iterator(m_ImageBufferPointer, m_ImageSize, m_OffsetTable, m_NeighborhoodAccessor, m_Location, m_ShapeOffsets);
  }

  /** Returns an 'end iterator' for this range. */
  iterator end() const ITK_NOEXCEPT
  {
    assert(m_ImageBufferPointer != nullptr);
    return iterator(m_ImageBufferPointer, m_ImageSize, m_OffsetTable, m_NeighborhoodAccessor, m_Location, m_ShapeOffsets + m_NumberOfNeighborhoodPixels);
  }

  /** Returns a const iterator to the first neighborhood pixel.
   * Provides only read-only access to the pixel data. */
  const_iterator cbegin() const ITK_NOEXCEPT
  {
    return this->begin();
  }

  /** Returns a const 'end iterator' for this range. */
  const_iterator cend() const ITK_NOEXCEPT
  {
    return this->end();
  }

  /** Returns a reverse 'begin iterator' for this range. */
  reverse_iterator rbegin() const ITK_NOEXCEPT
  {
    return reverse_iterator(this->end());
  }

  /** Returns a reverse 'end iterator' for this range. */
  reverse_iterator rend() const ITK_NOEXCEPT
  {
    return reverse_iterator(this->begin());
  }

  /** Returns a const reverse 'begin iterator' for this range. */
  const_reverse_iterator crbegin() const ITK_NOEXCEPT
  {
    return this->rbegin();
  }

  /** Returns a const reverse 'end iterator' for this range. */
  const_reverse_iterator crend() const ITK_NOEXCEPT
  {
    return this->rend();
  }


  /** Returns the size of the range, that is the number of neighborhood pixels. */
  std::size_t size() const ITK_NOEXCEPT
  {
    return m_NumberOfNeighborhoodPixels;
  }


  /** Subscript operator. Allows random access, to the nth neighbor pixel.
  * \note The return type QualifiedIterator<false>::reference is equivalent to
  * iterator::reference. The return value is a proxy object that behaves like a
  * reference to the pixel.
  */
  typename QualifiedIterator<false>::reference operator[](const std::size_t n) const ITK_NOEXCEPT
  {
    assert(n < this->size());
    assert(n <= static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()));

    return this->begin()[static_cast<std::ptrdiff_t>(n)];
  }


  /** Explicitly-defaulted destructor. */
  ~ShapedImageNeighborhoodRange() = default;
};


} // namespace Experimental
} // namespace itk

#endif