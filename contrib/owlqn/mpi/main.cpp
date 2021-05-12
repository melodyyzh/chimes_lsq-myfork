//
// Distributed Orthant-Wise Limited Memory Quasi-Newton.
// Modifications for MPI by Larry Fried
// 
#include <iostream>
#include <deque>
#include <fstream>
#include <cstring>
#include <string>

#ifdef USE_MPI
#include <mpi.h>
#endif

using namespace std;

#include "mpiexterns.h"
#include "OWLQN.h"
#include "leastSquares.h"
#include "logreg.h"



int NPROCS ;
int RANK ;


void printUsageAndExit() {
	if ( RANK == 0 ) {
		cout << "Orthant-Wise Limited-memory Quasi-Newton trainer" << endl;
		cout << "trains L1-regularized logistic regression or least-squares models" << endl << endl;
		cout << "usage: feature_file label_file regWeight output_file [options]" << endl;
		cout << "  feature_file   input feature matrix in Matrix Market format (mxn real coordinate or array)" << endl;
		cout << "                   rows represent features for each instance" << endl;
		cout << "  label_file     input instance labels in Matrix Market format (mx1 real array)" << endl;
		cout << "                   rows contain single real value" << endl;
		cout << "                   for logistic regression problems, value must be 1 or -1" << endl;
		cout << "  regWeight      coefficient of l1 regularizer" << endl;
		cout << "  output_file    output weight vector in Matrix Market format (1xm real array)" << endl << endl;
		cout << "options:" << endl;
		cout << "  -ls            use least squares formulation (logistic regression is default)" << endl;
		cout << "  -q             quiet.  Suppress all output" << endl;
		cout << "  -tol <value>   sets convergence tolerance (default is 1e-4)" << endl;
		cout << "  -m <value>     sets L-BFGS memory parameter (default is 10)" << endl;
		cout << "  -l2weight <value>" << endl;
		cout << "                 sets L2 regularization weight (default is 0)" << endl;
		cout << "  -init <file>" << endl;
		cout << "                 read initial parameter values from file" << endl ;
		cout << endl;
	}
	ErrorMsg("Ending Job") ;
}

void printVector(const DblVec &vec, const char* filename)
{
	string tempfile = "tempout.txt" ;
	ofstream outfile(tempfile) ;
	if (!outfile.good()) {
		string error = "error opening matrix file " + string(filename) + "\n" ;
		ErrorMsg(error) ;
	}
	outfile << "%%MatrixMarket matrix array real general" << endl;
	outfile.precision(13) ;
	outfile << "1 " << vec.size() << endl;
	outfile << scientific ;
	for (size_t i=0; i<vec.size(); i++) {
		outfile << vec[i] << endl;
	}
	outfile.flush() ;
	outfile.close();
	rename(tempfile.c_str(), filename) ;
}

int main(int argc, char* argv[]) {

	if (argc < 5 || !strcmp(argv[1], "-help") || !strcmp(argv[1], "--help") ||
		!strcmp(argv[1], "-h") || !strcmp(argv[1], "-usage")) {
			printUsageAndExit();
	}

	const char* feature_file = argv[1];
	const char* label_file = argv[2];
	double regweight = atof(argv[3]);
	const char* output_file = argv[4];

#ifdef USE_MPI	
	MPI_Init     (&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &NPROCS);
  MPI_Comm_rank(MPI_COMM_WORLD, &RANK);
#else
	RANK = 0 ;
	NPROCS = 1 ;
#endif	
	
	if (regweight < 0) {
		ErrorMsg("L1 regularization weight must be non-negative.") ;
	}

	bool leastSquares = true, quiet = false, split_files = false ;
	double tol = 1e-4, l2weight = 0;
	const char* init_file = NULL ;
	
	int m = 10;

	for (int i=5; i<argc; i++) {
		if (!strcmp(argv[i], "-ls")) leastSquares = true;
		else if (!strcmp(argv[i], "-q")) quiet = true;
		else if (!strcmp(argv[i], "-tol")) {
			++i;
			if (i >= argc || (tol = atof(argv[i])) <= 0) {
				ErrorMsg("-tol (convergence tolerance) flag requires 1 positive real argument.") ;
			}
		} else if (!strcmp(argv[i], "-l2weight")) {
			++i;
			if (i >= argc || (l2weight = atof(argv[i])) < 0) {
				ErrorMsg("-l2weight flag requires 1 non-negative real argument.");
			}
		}	else if (!strcmp(argv[i], "-m")) {
			++i;
			if (i >= argc || (m = atoi(argv[i])) == 0) {
				ErrorMsg("-m (L-BFGS memory param) flag requires 1 positive int argument.") ;
			}
		} else if ( ! strcmp(argv[i], "-s") ) {
			split_files = true ;
		} else if ( ! strcmp(argv[i], "-init") ) {
			++i ;
			init_file = argv[i] ;
		} else {
			ErrorMsg(string("unrecognized argument: ") + argv[i]) ;
		}
	}

	if (!quiet && RANK == 0) {
		cout << argv[0] << " called with arguments " << endl << "   ";
		for (int i=1; i<argc; i++) {
			cout << argv[i] << " ";
		}
		cout << endl;
	}

	size_t size;
	LeastSquaresProblem *prob ;
	LeastSquaresObjective *obj ;
	
	if (leastSquares) {
		prob = new LeastSquaresProblem(feature_file, label_file, split_files);
		obj = new LeastSquaresObjective(*prob, l2weight);
		size = prob->NumFeats(); 
	} else {
		ErrorMsg("Logistic regression not supported in MPI version") ;
	}

	DblVec init(size), ans(size), Ax( prob->NumInstances() );

	if ( init_file != NULL ) {
		read_mm_vector(init, init_file) ;
	}

	OWLQN opt(quiet);
	opt.Minimize(*obj, init, ans, regweight, tol, m);

	obj->Eval_Ax(ans, Ax) ;
	
	int nonZero = 0;
	for (size_t i = 0; i<ans.size(); i++) {
		if (ans[i] != 0) nonZero++;
	}


	if ( RANK == 0 ) {
		if (!quiet) cout << "Finished with optimization.  " << nonZero << "/" << size << " non-zero weights." << endl;
		printVector(ans, output_file);
		// print A * x, which is the current estimate of the supplied b (label) values.
		printVector(Ax, "Ax.txt") ;
	}

#ifdef USE_MPI	
	MPI_Finalize() ;
#endif
	
	return 0;
}

void ErrorMsg(string err)
{
	cerr << err << endl ;
#ifdef USE_MPI	
	MPI_Abort(MPI_COMM_WORLD,1) ;
#else
	exit(1) ;
#endif		
}

void exit_run(int status)
{
#ifdef USE_MPI	
	MPI_Abort(MPI_COMM_WORLD,status) ;
#else
	exit(status) ;
#endif	
}

void read_mm_vector(DblVec &vec, const char *filename)
// Read a matrix market vector.
{
	ifstream matfile(filename);
	if (!matfile.good()) {
		cerr << "error opening matrix file " << filename << endl;
		exit_run(1);
	}

	string s;
	getline(matfile, s);
	if (!s.compare("%%MatrixMarket matrix array real general")) {
		skipEmptyAndComment(matfile, s);
		stringstream st(s);
		int m, n ;
		st >> n >> m;

		if ( n != 1 ) {
			cerr << "The file did not contain 1 column" ;
			exit_run(1) ;
		}
		if ( m != vec.size() ) {
			cerr << "The file contained the wrong number of rows" ;
			exit_run(1) ;
		}

		for (size_t i=0; i<m; i++) {
			double val;
			matfile >> vec[i] ;
		}
		if ( ! matfile.good() ) {
			cerr << "Reading a vector failed" ;
			exit_run(1) ;
		}
		matfile.close();
	}
}