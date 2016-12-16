//
// IResearch search engine 
// 
// Copyright � 2016 by EMC Corporation, All Rights Reserved
// 
// This software contains the intellectual property of EMC Corporation or is licensed to
// EMC Corporation from third parties. Use of this software and the intellectual property
// contained therein is expressly limited to the terms and conditions of the License
// Agreement under which it is provided by or on behalf of EMC.
// 

#include "tests_shared.hpp"
#include "tests_config.hpp"

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include <ctime>
#include <formats/formats.hpp>

#include <utils/attributes.hpp>

#include "index/doc_generator.hpp"
#include "utils/log.hpp"
#include "utils/network_utils.hpp"
#include "utils/bitset.hpp"
#include "utils/runtime_utils.hpp"

#ifdef _MSC_VER
  #define make_temp(templ) _mktemp(templ)
#else
  #define make_temp(templ) mktemp(templ)
#endif

/* -------------------------------------------------------------------
* iteration_tracker
* ------------------------------------------------------------------*/

struct iteration_tracker : ::testing::Environment {
  virtual void SetUp() {
    ++iteration;
  }

  static uint32_t iteration;
};

uint32_t iteration_tracker::iteration = (std::numeric_limits<uint32_t>::max)();

/* -------------------------------------------------------------------
* test_base
* ------------------------------------------------------------------*/

namespace {
  namespace fs = boost::filesystem;
}

const std::string test_base::test_results( "test_detail.xml" );

fs::path test_base::exec_path_;
fs::path test_base::exec_dir_;
fs::path test_base::exec_file_;
fs::path test_base::out_dir_;
fs::path test_base::resource_dir_;
fs::path test_base::res_dir_;
fs::path test_base::res_path_;
std::string test_base::test_name_;
int test_base::argc_;
char** test_base::argv_;
decltype(test_base::argv_ires_output_) test_base::argv_ires_output_;

std::string test_base::temp_file() {
  return fs::unique_path().string();
}

uint32_t test_base::iteration() {
  return iteration_tracker::iteration;
}

std::string test_base::resource( const std::string& name ) {
  return fs::path( resource_dir_ ).append( name ).string();
}

void test_base::SetUp() {
  namespace tst = ::testing;
  const tst::TestInfo* ti = tst::UnitTest::GetInstance()->current_test_info();

  fs::path iter_dir( res_dir_ );
  if ( ::testing::FLAGS_gtest_repeat > 1 || ::testing::FLAGS_gtest_repeat < 0 ) {    
    iter_dir.append(
      std::string( "iteration " ).append( std::to_string( iteration() ) ) 
    );
  }

  ( test_case_dir_ = iter_dir ).append( ti->test_case_name() );
  ( test_dir_ = test_case_dir_ ).append( ti->name() );
  fs::create_directories( test_dir_ );
}

void test_base::prepare( const po::variables_map& vm ) {
  if ( vm.count( "help" ) ) {
    return;
  }

  make_directories();

  if (vm.count( "ires_output")) {
    std::unique_ptr<char*[]> argv(new char*[2 + argc_]);
    std::memcpy(argv.get(), argv_, sizeof(char*)*(argc_));    
    argv_ires_output_.append("--gtest_output=xml:").append(res_path_.string());
    argv[argc_++] = &argv_ires_output_[0];

    /* let last argv equals to nullptr */
    argv[argc_] = nullptr;
    argv_ = argv.release();
  }
}

void test_base::make_directories() {
  exec_path_ = fs::path( argv_[0] );
  exec_file_ = exec_path_.filename();
  exec_dir_ = exec_path_.parent_path();
  
  test_name_ = exec_file_.replace_extension().string();

  if (out_dir_.empty()) {
    out_dir_ = exec_dir_;
  }

  (res_dir_ = out_dir_).append( test_name_ );  
  // add timestamp to res_dir_
  {
    time_t rawtime;
    struct tm* tinfo;
    time(&rawtime);
    tinfo = iresearch::localtime(&rawtime);

    char buf[21]{};
    strftime(buf, sizeof buf, "_%Y_%m_%d_%H_%M_%S", tinfo);
    res_dir_.concat(buf, buf + sizeof buf - 1);
  }

  // add temporary string to res_dir_
  {
    char templ[] = "_XXXXXX";
    make_temp(templ);
    res_dir_.concat(templ, templ + sizeof templ - 1);
  }

  (res_path_ = res_dir_).append(test_results);
  fs::create_directories(res_dir_);
}

void test_base::parse_command_line( po::variables_map& vm ) {
  namespace po = boost::program_options;
  po::options_description desc( "\n[IReSearch] Allowed options" );
  desc.add_options()
    ( "help", "produce help message" )
    ( "ires_output", "generate an XML report" )
    ( "ires_output_path", po::value< fs::path >( &out_dir_ ), "set output directory" )
    ( "ires_resource_dir", po::value< fs::path >( &resource_dir_ ), "set resource directory" );

  po::command_line_parser parser( argc_, argv_ );
  parser.options( desc ).allow_unregistered();
  po::parsed_options options = parser.run();

  po::store( options, vm );
  po::notify( vm );

  if ( vm.count( "help" ) ) {
    std::cout << desc << std::endl;
    return;
  }

  if ( !vm.count( "ires_resource_dir" ) ) {
    resource_dir_ = IResearch_test_resource_dir;
  }
}

int test_base::initialize( int argc, char* argv[] ) {
  argc_ = argc;
  argv_ = argv;

  po::variables_map vm;
  parse_command_line( vm );
  prepare( vm );
  
  ::testing::AddGlobalTestEnvironment( new iteration_tracker() );
  ::testing::InitGoogleTest( &argc_, argv_ ); 

  // suppress log messages since tests check error conditions
  iresearch::logger::level(iresearch::logger::IRL_NONE);

  return RUN_ALL_TESTS();
}

/* -------------------------------------------------------------------
* main
* ------------------------------------------------------------------*/

int main( int argc, char* argv[] ) {
  const int code = test_base::initialize( argc, argv );

  std::cout << "Path to test result directory: " 
            << test_base::test_results_dir() 
            << std::endl;

  return code;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------