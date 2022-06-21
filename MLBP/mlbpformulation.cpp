#include "mlbpformulation.h"

#include "instance.h"
#include "solution.h"
#include "users.h"


void MLBPFormulation::createDecisionVariables(IloEnv env, const Instance<MLBP>& inst)
{
	// decision variables x_{ijk}
	x = IloArray<IloArray<IloNumVarArray>>(env, inst.m + 1);  // create array of decision variables (inst.m + 1 levels)
	for (int k : inst.M) if (k > 0) {
		x[k] = IloArray<IloNumVarArray>(env, inst.n[k - 1]);
		for (int i : inst.B[k - 1])  // for every index in i, create an array of size j
			x[k][i] = IloNumVarArray(env, inst.n[k], 0, 1, ILOBOOL);
	//                                       ^       ^  ^     ^
	//                                       |       |  |     +------ variable type: ILOBOOL for booleans
	//                                       |       |  +------------ maximum value of variables
	//                                       |       +--------------- minium value of variables
	//                                       +----------------------- number of variables
	}
	int num = 0; for (int k : inst.M) num += inst.n[k - 1] * inst.n[k];
	SOUT() << "created " << num << " x_{ijk} variables" << std::endl;

	// decision variables y_{jk}
	y = IloArray<IloNumVarArray>(env, inst.m + 1);
	for (int k : inst.M) if (k > 0) 
		y[k] = IloNumVarArray(env, inst.n[k], 0, 1, ILOBOOL);

	num = 0; for (int k : inst.M) num += inst.n[k];
	SOUT() << "created " << num << " y_{jk} variables" << std::endl;
}

void MLBPFormulation::addConstraints(IloEnv env, IloModel model, const Instance<MLBP>& inst)
{
	// each item must be inserted into exactly one bin at level 1
	for (int i : inst.B[0]) {
		IloExpr sum(env);  // represents a linear expression of decision variables and constants
		for (int j : inst.B[1])
			sum += x[1][i][j];  // cplex overloads +,-,... operators
		model.add(sum == 1);    // add boolean constraint to model
		sum.end();  // IloExpr must always call end() to free memory!
	}

	// if a bin at level k - 1 is used, it must be packed into a bin at level k
	for (int k : inst.M) if (k > 1)
		for (int i : inst.B[k - 1]) {
			IloExpr sum(env);
			for (int j : inst.B[k])
				sum += x[k][i][j];          // bin i is packed =(1) or not =(0)
			model.add(sum == y[k - 1][i]);  // sum should only be 1 if bin i was used
			sum.end();
		}
	int num = 0; for (int k : inst.M) num += inst.n[k - 1];
	SOUT() << "added " << num << " constraints to enforce the packing of each item" << std::endl;

	// the size of the content of a bin must not exceed the bin's capacity
	for (int k : inst.M) if (k > 0)
		for (int j : inst.B[k]) {
			IloExpr sum(env);
			for (int i : inst.B[k - 1])
				sum += x[k][i][j] * inst.s[k - 1][i];  // add the size of the item i that was added to j at level k
			model.add(sum <= y[k][j] * inst.w[k][j]);  // the total size of items in j should be less than the capacity
			sum.end();
		}
	num = 0; for (int k : inst.M) num += inst.n[k];
	SOUT() << "added " << num << " capacity constraints" << std::endl;

	// implicated LP-Relaxation constraint
	for (int k : inst.M) if (k > 1)
		for (int j : inst.B[k])
			for (int i : inst.B[k - 1])
				model.add(x[k][i][j] <= y[k - 1][i]);
}

void MLBPFormulation::addObjectiveFunction(IloEnv env, IloModel model, const Instance<MLBP>& inst)
{
	IloExpr sum(env);
	for (int k : inst.M)
		for (int j : inst.B[k])
			sum += y[k][j] * inst.c[k][j];  // minimise the total cost of all used bins
	model.add(IloMinimize(env, sum)); sum.end();
	SOUT() << "added objective function" << std::endl;
}

void MLBPFormulation::extractSolution(IloCplex cplex, const Instance<MLBP>& inst, Solution<MLBP>& sol)
{
	// cplex.getValue(x) returns the assigned value of decision variable x
	sol.total_bin_cost = 0;
	for (int k : inst.M) if (k > 0) {
		sol.item_to_bins[k - 1].assign(inst.n[k - 1], -1);  // initialise array with value -1
		for (int j : inst.B[k]) {  // for every bin add cost to total if bin j is used
			if (cplex.getValue(y[k][j]) > 0.5)
				sol.total_bin_cost += inst.c[k][j];
			for (int i : inst.B[k - 1])  // for every outgoing edge store the target node
				if (cplex.getValue(x[k][i][j]) > 0.5)
					sol.item_to_bins[k - 1][i] = j;
		}
	}
}

