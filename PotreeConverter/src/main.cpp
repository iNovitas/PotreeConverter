
#include <chrono>
#include <vector>
#include <map>
#include <string>
#include <exception>
#include <fstream>

#include "AABB.h"
#include "PotreeConverter.h"
#include "PotreeException.h"

#include "boost/program_options.hpp" 
#include <boost/filesystem.hpp>

namespace po = boost::program_options; 
namespace fs = boost::filesystem;

using std::string;
using std::cout;
using std::cerr;
using std::endl;
using std::vector;
using std::binary_function;
using std::map;
using std::chrono::high_resolution_clock;
using std::chrono::milliseconds;
using std::chrono::duration_cast;
using std::exception;
using Potree::PotreeConverter;
using Potree::StoreOption;
using Potree::ConversionQuality;

#define MAX_FLOAT std::numeric_limits<float>::max()

class SparseGrid;

void printUsage(po::options_description &desc){
	cout << "usage: PotreeConverter [OPTIONS] SOURCE" << endl;
	cout << desc << endl;
}

// from http://stackoverflow.com/questions/15577107/sets-of-mutually-exclusive-options-in-boost-program-options
void conflicting_options(const boost::program_options::variables_map & vm, const std::string & opt1, const std::string & opt2){
    if (vm.count(opt1) && !vm[opt1].defaulted() && vm.count(opt2) && !vm[opt2].defaulted()){
        throw std::logic_error(std::string("Conflicting options '") + opt1 + "' and '" + opt2 + "'.");
    }
}

struct Arguments{
	bool help = false;
	StoreOption storeOption = StoreOption::ABORT_IF_EXISTS;
	vector<string> source;
	string outdir;
    double unitScale;
    vector<double> globalShift;
    float spacing;
	int levels;
	string format;
	string outFormatString;
	double scale;
	int diagonalFraction;
	Potree::OutputFormat outFormat;
	vector<double> colorRange;
	vector<double> intensityRange;
	vector<string> outputAttributes;
	bool generatePage;
	string aabbValuesString;
	vector<double> aabbValues;
	string pageName = "";
	string projection = "";
	bool sourceListingOnly = false;
	string listOfFiles = "";
	ConversionQuality conversionQuality = ConversionQuality::DEFAULT;
	string conversionQualityString = "";
	string title = "PotreeViewer";
	string description = "";
	bool edlEnabled = false;
	bool showSkybox = false;
	string material = "RGB";
    
};

Arguments parseArguments(int argc, char **argv){
	Arguments a;

	po::options_description desc("Options"); 
	desc.add_options() 
		("help,h", "prints usage")
		("generate-page,p", po::value<string>(&a.pageName), "Generates a ready to use web page with the given name.")
		("outdir,o", po::value<string>(&a.outdir), "output directory") 
        ("unit-scale,u", po::value<double> (&a.unitScale), "Scale applied to coordinates to switch between different units. Default is 1.")
        ("global-shift,g", po::value<std::vector<double> >()->multitoken(), "Global shift of coordinates (after scaling!) to avoid digit overflow. Default is (0,0,0).")        
		("spacing,s", po::value<float>(&a.spacing), "Distance between points at root level. Distance halves each level.") 
		("spacing-by-diagonal-fraction,d", po::value<int>(&a.diagonalFraction), "Maximum number of points on the diagonal in the first level (sets spacing). spacing = diagonal / value")
		("levels,l", po::value<int>(&a.levels), "Number of levels that will be generated. 0: only root, 1: root and its children, ...")
		("input-format,f", po::value<string>(&a.format), "Input format. xyz: cartesian coordinates as floats, rgb: colors as numbers, i: intensity as number")
		("color-range", po::value<std::vector<double> >()->multitoken(), "")
		("intensity-range", po::value<std::vector<double> >()->multitoken(), "")
		("output-format", po::value<string>(&a.outFormatString), "Output format can be BINARY, LAS or LAZ. Default is BINARY")
		("output-attributes,a", po::value<std::vector<std::string> >()->multitoken(), "can be any combination of RGB, INTENSITY, TIMESTAMP and CLASSIFICATION. Default is RGB.")
		("scale", po::value<double>(&a.scale), "Scale of the X, Y, Z coordinate in LAS and LAZ files.")
		("aabb", po::value<string>(&a.aabbValuesString), "Bounding cube as \"minX minY minZ maxX maxY maxZ\". If not provided it is automatically computed")
		("incremental", "Add new points to existing conversion")
		("overwrite", "Replace existing conversion at target directory")
		("source-listing-only", "Create a sources.json but no octree.")
		("projection", po::value<string>(&a.projection), "Specify projection in proj4 format.")
		("quality,q", po::value<string>(&a.conversionQualityString), "Specify FAST, DEFAULT or NICE to trade-off between quality and conversion speed.")
		("list-of-files", po::value<string>(&a.listOfFiles), "A text file containing a list of files to be converted.")
		("source", po::value<std::vector<std::string> >(), "Source file. Can be LAS, LAZ, PTX or PLY")
		("title", po::value<string>(&a.title), "Page title")
		("description", po::value<string>(&a.description), "Description to be shown in the page.")
		("edl-enabled", "Enable Eye-Dome-Lighting.")
		("show-skybox", "")
		("material", po::value<string>(&a.material), "RGB, ELEVATION, INTENSITY, INTENSITY_GRADIENT, RETURN_NUMBER, SOURCE, TIMESTAMP, LEVEL_OF_DETAIL");
	po::positional_options_description p; 
	p.add("source", -1); 

	po::variables_map vm; 
	po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm); 
	po::notify(vm);

	if(vm.count("help") || (!vm.count("source") && !vm.count("list-of-files"))){
		printUsage(desc);
		exit(0);
	}

	conflicting_options(vm, "incremental", "overwrite");

	if(vm.count("incremental")){
		a.storeOption = StoreOption::INCREMENTAL;
	}else if(vm.count("overwrite")){
		a.storeOption = StoreOption::OVERWRITE;
	}else{
		a.storeOption = StoreOption::ABORT_IF_EXISTS;
	}

	if(vm.count("source-listing-only")){
		a.sourceListingOnly = true;
	}

	if(vm.count("edl-enabled")){
		a.edlEnabled = true;
	}else{
		a.edlEnabled = false;
	}

	if(vm.count("show-skybox")){
		a.showSkybox = true;
	}else{
		a.showSkybox = false;
	}

	vector<string> validMaterialNames{"RGB", "ELEVATION", "INTENSITY", "INTENSITY_GRADIENT", "RETURN_NUMBER", "SOURCE", "LEVEL_OF_DETAIL"};
	if(std::find(validMaterialNames.begin(), validMaterialNames.end(), a.material) == validMaterialNames.end()){
		printUsage(desc);
		cout << endl;
		cout << "ERROR: " << "invalid material name specified" << endl;
		exit(1);
	}

	if(vm.count("source")){
		a.source = vm["source"].as<std::vector<std::string> >();
	}else if(vm.count("list-of-files")){
		if(fs::exists(fs::path(a.listOfFiles))){
			std::ifstream in(a.listOfFiles);
			string line;
			while( std::getline(in, line)){
				string path;
				if(fs::path(line).is_absolute()){
					path = line;
				}else{
					fs::path absPath = fs::canonical(fs::path(a.listOfFiles));
					fs::path lofDir = absPath.parent_path();
					path = lofDir.string() + "/" + line;
				}
				
				if(fs::exists(fs::path(path))){
					a.source.push_back(path);
				}else{
					cerr << "ERROR: file not found: " << path << endl;
					exit(1);
				}
			}
			in.close();
		}else{
			cerr << "ERROR: specified list of files not found: '" << a.listOfFiles << "'" << endl;
			exit(1);
		}
	}else{
		cerr << "ERROR: neither source file nor list-of-files parameters were specified!" << endl;
		exit(1);
	}

    if (vm.count ("global-shift")){
        a.globalShift = vm ["global-shift"].as< vector<double> > ();

        if (a.globalShift.size () != 3){
            cerr << "global-shift only takes 3 arguments (dx, dy, dz)" << endl;
            exit (1);
        }
    }
    else {
        a.globalShift = std::vector<double> (3, 0.);
    }

	if(vm.count("color-range")){
		a.colorRange = vm["color-range"].as< vector<double> >();

		if(a.colorRange.size() > 2){
			cerr << "color-range only takes 0 - 2 arguments" << endl;
			exit(1);
		}
	}

	if(vm.count("intensity-range")){
		a.intensityRange = vm["intensity-range"].as< vector<double> >();

		if(a.intensityRange.size() > 2){
			cerr << "intensity-range only takes 0 - 2 arguments" << endl;
			exit(1);
		}
	}

	if(vm.count("output-attributes")){
		a.outputAttributes = vm["output-attributes"].as< vector<string> >();
	}else{
		a.outputAttributes.push_back("RGB");
	}


	if(vm.count("aabb")){
		char sep = ' '; 
		for(size_t p=0, q=0; p!= a.aabbValuesString.npos; p=q)
    		a.aabbValues.push_back(atof(a.aabbValuesString.substr(p+(p!=0), (q = a.aabbValuesString.find(sep, p+1))-p-(p!=0)).c_str())); 

		if(a.aabbValues.size() != 6){
			cerr << "AABB requires 6 arguments" << endl;
			exit(1);
		}
	}

	// set default parameters 
	fs::path pSource(a.source[0]);
	a.outdir = vm.count("outdir") ? vm["outdir"].as<string>() : pSource.generic_string() + "_converted";
    if(!vm.count ("unit-scale")) a.unitScale = 1.;
	if(!vm.count("spacing")) a.spacing = 0;
	a.generatePage = (!vm.count("generate-page")) ? false : true;
	if(!vm.count("spacing-by-diagonal-fraction")) a.diagonalFraction = 0;
	if(!vm.count("levels")) a.levels = -1;
	if(!vm.count("input-format")) a.format = "";
	if(!vm.count("scale")) a.scale = 0;
	if(!vm.count("output-format")) a.outFormatString = "BINARY";
	if(!vm.count("output-format")) a.conversionQualityString = "DEFAULT";
	
	if(a.outFormatString == "BINARY"){
		a.outFormat = Potree::OutputFormat::BINARY;
	}else if(a.outFormatString == "LAS"){
		a.outFormat = Potree::OutputFormat::LAS;
	}else if(a.outFormatString == "LAZ"){
		a.outFormat = Potree::OutputFormat::LAZ;
	}

	if(a.conversionQualityString == "FAST"){
		a.conversionQuality = ConversionQuality::FAST;
	}else if(a.conversionQualityString == "DEFAULT"){
		a.conversionQuality = ConversionQuality::DEFAULT;
	}else if(a.conversionQualityString == "NICE"){
		a.conversionQuality = ConversionQuality::NICE;
	}
	
	if (a.diagonalFraction != 0) {
		a.spacing = 0;
	}else if(a.spacing == 0){
		a.diagonalFraction = 200;
	}

	return a;
}

void printArguments(Arguments &a){
	try{

		cout << "== params ==" << endl;
		int i = 0;
		for(const auto &s : a.source) {
			cout << "source[" << i << "]:         \t" << a.source[i] << endl;
			++i;
		}
		cout << "outdir:            \t" << a.outdir << endl;
        cout << "unit-scale:        \t" << a.unitScale << endl;
        cout << "global-shift:      \t" << a.globalShift[0] << " " << a.globalShift [1] << " " << a.globalShift [2] << endl;
        cout << "spacing:           \t" << a.spacing << endl;
		cout << "diagonal-fraction: \t" << a.diagonalFraction << endl;
		cout << "levels:            \t" << a.levels << endl;
		cout << "format:            \t" << a.format << endl;
		cout << "scale:             \t" << a.scale << endl;
		cout << "pageName:          \t" << a.pageName << endl;
		cout << "output-format:     \t" << a.outFormatString << endl;
		cout << "projection:        \t" << a.projection << endl;
		cout << endl;
	}catch(exception &e){
		cout << "ERROR: " << e.what() << endl;

		exit(1);
	}
}

#include "Vector3.h"
#include <random>

//int main(int argc, char **argv){
//
//	auto start = high_resolution_clock::now();
//
//	int numPoints = 1'000'000;
//
//	std::default_random_engine generator;
//	std::uniform_int_distribution<int> distribution(-10, 10);
//
//	vector<Potree::Vector3<double>> points;
//
//	for(int i = 0; i < numPoints; i++){
//		double x = distribution(generator);
//		double y = distribution(generator);
//		double z = distribution(generator);
//
//		Potree::Vector3<double> point(x, y, z);
//		points.push_back(point);
//	}
//
//
//	double minDistance = 1000.0;
//	Potree::Vector3<double> pref(7, 3, 9);
//	for(int j = 0; j < 100; j++){
//		for(int i = 0; i < numPoints; i++){
//			double distance = points[i].distanceTo(pref);
//			minDistance = min(distance, minDistance);
//		}
//	}
//
//	cout << "min distance: " << minDistance << endl;
//
//
//	auto end = high_resolution_clock::now();
//	long long duration = duration_cast<milliseconds>(end-start).count();
//	float seconds = duration / 1'000.0f;
//
//	cout << "duration: " << seconds << endl;
//
//}

int main(int argc, char **argv){
	cout.imbue(std::locale(""));
	
	
	
	try{
		Arguments a = parseArguments(argc, argv);
		printArguments(a);

		PotreeConverter pc(a.outdir, a.source);

        pc.unitScale = a.unitScale;
        pc.globalShift = a.globalShift;
        pc.spacing = a.spacing;
		pc.diagonalFraction = a.diagonalFraction;
		pc.maxDepth = a.levels;
		pc.format = a.format;
		pc.colorRange = a.colorRange;
		pc.intensityRange = a.intensityRange;
		pc.scale = a.scale;
		pc.outputFormat = a.outFormat;
		pc.outputAttributes = a.outputAttributes;
		pc.aabbValues = a.aabbValues;
		pc.pageName = a.pageName;
		pc.storeOption = a.storeOption;
		pc.projection = a.projection;
		pc.sourceListingOnly = a.sourceListingOnly;
		pc.quality = a.conversionQuality;
		pc.title = a.title;
		pc.description = a.description;
		pc.edlEnabled = a.edlEnabled;
		pc.material = a.material;
		pc.showSkybox = a.showSkybox;

		pc.convert();
	}catch(exception &e){
		cout << "ERROR: " << e.what() << endl;
		return 1;
	}
	
	return 0;
}

