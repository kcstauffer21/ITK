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

#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"

#include "itkSimpleFilterWatcher.h"
#include "itkLabelContourImageFilter.h"
#include "itkTestingMacros.h"

int itkLabelContourImageFilterTest(int argc, char * argv[])
{
  if( argc != 5 )
    {
    std::cerr << "usage: " << itkNameOfTestExecutableMacro(argv) << " intput output fullyConnected bg" << std::endl;
    std::cerr << " input: the input image" << std::endl;
    std::cerr << " output: the output image" << std::endl;
    std::cerr << " fullyConnected: 0 or 1" << std::endl;
    return EXIT_FAILURE;
    }

  constexpr int dim = 3;

  using PType = unsigned char;
  using IType = itk::Image< PType, dim >;

  using ReaderType = itk::ImageFileReader< IType >;
  ReaderType::Pointer reader = ReaderType::New();
  reader->SetFileName( argv[1] );

  using FilterType = itk::LabelContourImageFilter< IType, IType >;
  FilterType::Pointer filter = FilterType::New();

  // test default values
  if ( filter->GetFullyConnected( ) != false )
    {
    std::cerr << "Wrong default FullyConnected." << std::endl;
    return EXIT_FAILURE;
    }
  if ( filter->GetBackgroundValue( ) != 0 )
    {
    std::cerr << "Wrong default background value." << std::endl;
    return EXIT_FAILURE;
    }

  // Tests for raising code coverage
  ITK_EXERCISE_BASIC_OBJECT_METHODS(filter, LabelContourImageFilter, InPlaceImageFilter);
  ITK_TRY_EXPECT_EXCEPTION(filter->Update());
  ITK_TEST_SET_GET_BOOLEAN(filter, FullyConnected, std::stoi(argv[3]));

  filter->SetInput( reader->GetOutput() );

  filter->SetBackgroundValue( std::stoi(argv[4]) );
  if ( filter->GetBackgroundValue( ) != std::stoi(argv[4]) )
    {
    std::cerr << "Set/Get BackgroundValue problem." << std::endl;
    return EXIT_FAILURE;
    }

  itk::SimpleFilterWatcher watcher(filter, "filter");

  using WriterType = itk::ImageFileWriter< IType >;
  WriterType::Pointer writer = WriterType::New();
  writer->SetInput( filter->GetOutput() );
  writer->SetFileName( argv[2] );

  ITK_TRY_EXPECT_NO_EXCEPTION(writer->Update());

  return EXIT_SUCCESS;
}
