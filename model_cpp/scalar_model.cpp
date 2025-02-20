#include "scalar_model.h"
#include <stdlib.h>
#include <math.h>
#include <iostream>
#include <fstream>
#include <string>
#include <random>

using namespace std;
default_random_engine generator2(5);
uniform_real_distribution<double> uni_distribution2(0.0, 1.0);


/**
  * Constructor of Scalar Model
  *
  * The scalar model contains all the cells and sources of the 2D model inside a single pixel
  * The constructor doesn't actually create the simuation as the agent will always first call reset() on the scalar model.
  *
  */
ScalarModel::ScalarModel(char reward): reward(reward), cancer_cells(nullptr), healthy_cells(nullptr), time(0), glucose(0.0), oxygen(0.0), end_type('0'), init_hcell_count(0){
}

/**
  * Destructor of the scalar model
  *
  */
ScalarModel::~ScalarModel(){
    delete cancer_cells;
    delete healthy_cells;
}

/**
  *
  * Starts the simulation until the time where the treatment can start (350 hours, cancer cells outnumber healthy cells)
  *
  */
void ScalarModel::reset(){
    delete cancer_cells;
    delete healthy_cells;
    HealthyCell::count = 0;
    CancerCell::count = 0;
    time = 0;
    glucose = 250000.0;
    oxygen = 2500000.0;
    healthy_cells = new CellList();
    cancer_cells = new CellList();
    for(int i = 0; i < 1000; i++)
        healthy_cells -> add(new HealthyCell('1'), 'h');
    cancer_cells -> add(new CancerCell('1'), 'c');
    go(350);
    init_hcell_count = HealthyCell::count;
}

/**
 * Go through all cells in the model and advance them by one hour in their cycle
 *
 */
void ScalarModel::cycle_cells(){
    int hcell_count = HealthyCell::count;
    int ccell_count = CancerCell::count;
    int count = hcell_count + ccell_count;
    CellNode * current_h = healthy_cells -> head;
    CellNode * current_c = cancer_cells -> head;
    CellNode * current;
    while(hcell_count > 0 || ccell_count > 0){
        if (rand() % (hcell_count + ccell_count) < ccell_count){
            ccell_count--;
            current = current_c;
            current_c = current_c -> next;
        } else {
            hcell_count--;
            current = current_h;
            current_h = current_h -> next;
        }
        cell_cycle_res result = current->cell->cycle(glucose, oxygen, count / 278);
        glucose -= result.glucose;
        oxygen -= result.oxygen;
        if (result.new_cell == 'h') //New healthy cell
            healthy_cells -> add(new HealthyCell('1'), 'h');
        else if (result.new_cell == 'c') // New cancer cell
            cancer_cells -> add(new CancerCell('1'), 'c');
    }
    healthy_cells -> deleteDeadAndSort();
    cancer_cells -> deleteDeadAndSort();
}

/**
  * Add new nutrients to the model
  *
  */
void ScalarModel::fill_sources(){
    glucose += 13000.0;
    oxygen += 450000.0;
}

/**
  * Simulate one hour
  */
void ScalarModel::go(int i){
    for(int x = 0; x < i; x++){
        time++;
        fill_sources();
        cycle_cells();
    }
}

/**
 * Irradiate all cells in the model with a certain dose
 *
 * @param dose The dose of radiation (in grays)
 */
void ScalarModel::irradiate(int dose){
    CellNode * current_h = healthy_cells -> head;
    while(current_h){
        current_h -> cell -> radiate(dose);
        current_h = current_h -> next;
    }
    healthy_cells -> deleteDeadAndSort();
    CellNode * current_c = cancer_cells -> head;
    while(current_c){
        current_c -> cell -> radiate(dose);
        current_c = current_c -> next;
    }
    cancer_cells -> deleteDeadAndSort();
}

/**
  * Apply the action selected by the agent and let 24 hours pass
  *
  * This action will be the irradiation dose -1 (as the action space starts at 0 Gy)
  */
double ScalarModel::act(int action){
    int dose = action + 1;
    int pre_hcell = HealthyCell::count;
    int pre_ccell = CancerCell::count;
    irradiate(dose);
    int m_hcell = HealthyCell::count;
    int m_ccell = CancerCell::count;
    go(24);
    int post_hcell = HealthyCell::count;
    int post_ccell = CancerCell::count;
    return adjust_reward(dose, pre_ccell - post_ccell, pre_hcell-min(post_hcell, m_hcell));
}

/**
  * Compute the reward to the agent after the action was applied to the environment
  *
  */
double ScalarModel::adjust_reward(int dose, int ccell_killed, int hcells_lost){
    //cout << dose << " " << reward << " " << ccell_killed << " " << hcells_lost << endl;
    if (inTerminalState() && reward != 'n'){
        if (end_type == 'L' || end_type == 'T'){
            return -1.0;
        } else{
            if (reward == 'd')
                return - (double) dose / 200.0 + 0.5 + (double) (HealthyCell::count) / 4000.0;
            else
                return 0.5 + (double) (HealthyCell::count) / 4000.0;
        }
    } else {
        if (reward == 'd' || reward == 'n')
            return - (double) dose / 200.0 + (double) (ccell_killed - 5.0* hcells_lost)/100000.0;
        else{
	    //cout << ccell_killed << " " << hcells_lost <<endl;
            return (double) (ccell_killed - (double) (reward - '0') * hcells_lost)/100000.0;
	    }
    }
}

/**
  * Returns true if the simulation has reached a terminal state
  *
  */
bool ScalarModel::inTerminalState(){
    if (CancerCell::count <= 0){
        end_type = 'W';
        return true;
    } else if (HealthyCell::count < 10){
        end_type = 'L';
        return true;
    } else if (time > 1550){
        end_type = 'T';
        return true;
    } else {
        return false;
    }
}

TabularAgent::TabularAgent(ScalarModel * env, int cancer_cell_stages, int healthy_cell_stages, int actions, char state_type): env(env), cancer_cell_stages(cancer_cell_stages), healthy_cell_stages(healthy_cell_stages), actions(actions), state_type(state_type){
    Q_values = new double*[cancer_cell_stages * healthy_cell_stages];
    for(int i = 0; i < cancer_cell_stages * healthy_cell_stages; i++){
        Q_values[i] = new double[actions]();
    }
    if(state_type == 'o') { //log
        state_helper_hcells = exp(log(3500.0) / ((double) healthy_cell_stages - 2.0));
        state_helper_ccells = exp(log(40000.0) / ((double) cancer_cell_stages - 2.0));
    } else { // lin
        state_helper_hcells = 3500.0 / ((double) healthy_cell_stages - 2.0);
        state_helper_ccells = 40000.0 / ((double) cancer_cell_stages - 2.0);
    }
}

TabularAgent::~TabularAgent(){
    for(int i = 0; i < cancer_cell_stages * healthy_cell_stages; i++){
        delete[] Q_values[i];
    }
    delete[] Q_values;
}

int TabularAgent::state(){
    int hcell_state, ccell_state;
    if(state_type == 'o') { //log
        ccell_state = min(cancer_cell_stages - 1, (int) ceil(log(CancerCell::count + 1) / log(state_helper_ccells)));
        hcell_state = min(healthy_cell_stages - 1, (int) ceil(log(max(HealthyCell::count - 8, 1)) / log(state_helper_hcells)));
    } else{
        ccell_state = min(cancer_cell_stages - 1, (int) ceil((double) CancerCell::count / (double) state_helper_ccells) );
        hcell_state = min(healthy_cell_stages - 1, (int) ceil((double)  max(HealthyCell::count - 9, 0) / (double) state_helper_hcells) );
    }
    return ccell_state * healthy_cell_stages + hcell_state;
}

int TabularAgent::choose_action(int state, double epsilon){
    if(uni_distribution2(generator2) < epsilon) {
        return rand() % actions;
    } else {
        int max_ind = -1;
        double max_val = - 999999.0;
        for(int i = 0; i < actions; i++){
            if(max_val < Q_values[state][i]) {
                max_val = Q_values[state][i];
                max_ind = i;
            }
        }
        return max_ind;
    }
}

void TabularAgent::train(int steps, double alpha, double epsilon, double disc_factor){
    env -> reset();
    while(steps > 0){
        while (!env->inTerminalState() && steps > 0){
            int obs = state();
            int action = choose_action(obs, epsilon);
            double r = env->act(action);
            int new_obs = state();
            double max_val = - 99999.0;
            for(int i = 0; i < actions; i++){
                if(Q_values[new_obs][i] > max_val)
                    max_val = Q_values[new_obs][i];
            }
            Q_values[obs][action] = (1.0 - alpha) * Q_values[obs][action] + alpha * (r + disc_factor * max_val);
            steps--;
        }
        if(steps > 0)
            env -> reset();
    }
}

void TabularAgent::test(int episodes, bool verbose, double disc_factor, bool eval){
    double sum_scores = 0.0;
    double sum_error = 0.0;
    int sum_length = 0;
    int squared_length = 0;
    int sum_fracs = 0;
    int squared_fracs = 0.0;
    int sum_doses = 0;
    int squared_doses = 0.0;
    int sum_w = 0;
    double sum_survival = 0.0;
    double squared_survival = 0.0;
    for (int i = 0; i < episodes; i++){
        env -> reset();
        double sum_r = 0;
        double err = 0.0;
        int count = 0;
        int fracs = 0;
        int doses = 0;
        int time = 0;
        int init_hcell = HealthyCell::count;
        while (!env->inTerminalState()){
            int obs = state();
            int action = choose_action(obs, 0.0);
            double r = env -> act(action);
            if (verbose)
                cout << action + 1 << " grays, reward =  " << r << endl;
            fracs++;
            doses += action + 1;
            time += 24;
            sum_r += r;
            int new_obs = state();
            double max_val = - 99999.0;
            for(int i = 0; i < actions; i++){
                if(Q_values[new_obs][i] > max_val)
                    max_val = Q_values[new_obs][i];
            }
            err += pow(r + disc_factor * max_val - Q_values[obs][action], 2.0);
            count++;
        }
        if(verbose)
            cout << env -> end_type << endl;
        if (env -> end_type == 'W')
            sum_w++;
        if (eval){
            sum_fracs += fracs;
            squared_fracs += fracs * fracs;
            sum_doses += doses;
            squared_doses += doses*doses;
            sum_length += time;
            squared_length += time*time;
            double survival = (double) HealthyCell::count / (double) init_hcell;
            sum_survival += survival;
            squared_survival += survival * survival;
        }
        sum_scores += sum_r;
        sum_error += err / (double) count;
    }
    cout << "Average score: " << sum_scores / (double) episodes << " MSE: " << sum_error / (double) episodes << endl;
    if(eval){
        cout << "TCP: " << 100.0 * (double) sum_w / (double) episodes << endl;

        double mean_frac = (double) sum_fracs / (double) episodes;
        double std_frac = sqrt(((double) squared_fracs / (double) episodes) - (mean_frac * mean_frac));
        cout << "Average num of fractions: " << mean_frac << " std dev: "<< std_frac <<endl;

        double mean_dose = (double) sum_doses / (double) episodes;
        double std_dose = sqrt(((double) squared_doses / (double) episodes) - (mean_dose * mean_dose));
        cout << "Average radiation dose: " << mean_dose << " std dev: "<< std_dose <<endl;

        double mean_duration = (double) sum_length / (double) episodes;
        double std_duration = sqrt(((double) squared_length / (double) episodes) - (mean_duration * mean_duration));
        cout << "Average duration: " << mean_duration << " std dev: "<< std_duration <<endl;

        double mean_survival = (double) sum_survival / (double) episodes;
        double std_survival = sqrt(((double) squared_survival / (double) episodes) - (mean_survival * mean_survival));
        cout << "Average survival: " << mean_survival << " std dev: "<< std_survival <<endl;
    }
}

void TabularAgent::run(int n_epochs, int train_steps, int test_steps, double init_alpha, double alpha_mult, double init_epsilon, double end_epsilon, double disc_factor){
    test(test_steps, false, disc_factor, false);
    double alpha = init_alpha;
    double epsilon = init_epsilon;
    double epsilon_change = (double) (init_epsilon - end_epsilon) / (double) (n_epochs - 1);
    double alpha_change = (double) (init_alpha - alpha_mult) / (double) (n_epochs - 1);
    for(int i = 0; i < n_epochs; i++){
        cout << "Epoch " << i + 1 << endl;
        train(train_steps, alpha, epsilon, disc_factor);
        test(test_steps, false, disc_factor, false);
        alpha -= alpha_change;
        epsilon -= epsilon_change;
    }
}

void TabularAgent::treatment_var(int count){
    int** treatments = new int*[count];
    for(int i = 0; i < count; i++){
        treatments[i] = new int[100]();
    }
    for(int i = 0; i < count; i++){
        env -> reset();
        int j = 0;
        while (!env->inTerminalState()){
            int obs = state();
            int action = choose_action(obs, 0.0);
            double r = env -> act(action);
            treatments[i][j++] = action + 1;
        }
    }
    cout << "count, mean, std_error" << endl;
    for(int j = 0; j < 100; j++){
        int count_mean = 0;;
        int sum_mean = 0;
        for(int i = 0; i < count; i++){
            if (treatments[i][j] > 0){
                sum_mean += treatments[i][j];
                count_mean++;
            }
        }
        if (count_mean == 0)
            break;
        double mean = (double) sum_mean / (double) count;
        double sum_std = 0.0;
        for(int i = 0; i < count; i++){
            //if (treatments[i][j] > 0){
                sum_std += pow(((double) treatments[i][j]) - mean, 2.0);
            //}
        }
        double std_error = sqrt(sum_std / (double) count);
        cout << count_mean << ", " << mean << ", " << std_error << endl;
    }
    for(int i = 0; i < count; i++){
        delete[] treatments[i];
    }
    delete[] treatments;
}

void TabularAgent::save_Q(string name){
    ofstream myfile;
    myfile.open(name);
    myfile << cancer_cell_stages << " " << healthy_cell_stages << " " << actions << "\n";
    for(int i = 0; i < cancer_cell_stages * healthy_cell_stages; i++){
        for(int j = 0; j < actions; j++){
            myfile << Q_values[i][j] << ", ";
        }
        myfile << "\n";
    }
    myfile.close();
}

void TabularAgent::load_Q(string name){
    ifstream f;
    //cout << "Trying to open "<< name << endl; 
    f.open(name);
    if(!f.is_open()) throw std::runtime_error("Could not open file");
    //cout << "opened" <<endl;
    string line;
    getline(f, line);
    //cout << "First line : "<< line << endl;
    int pos_space_1 = line.find(" ");
    int pos_space_2 = line.find(" ", pos_space_1 + 1);
    int t_cstage = stoi(line.substr(0, pos_space_1));
    int t_hstage = stoi(line.substr(pos_space_1 + 1, pos_space_2 - pos_space_1 - 1));
    int t_actions = stoi(line.substr(pos_space_2 + 1));
    //cout << t_cstage << " " << t_hstage << " " << t_actions << " " << pos_space_1 << " " << pos_space_2 << endl;
    if(t_cstage != cancer_cell_stages || t_hstage != healthy_cell_stages || t_actions != actions)
        throw std::runtime_error("Parameters do not match");
    //getline(f, line);
    //cout << "Second line [" << line << "]" << endl;
    for(int i = 0; i < cancer_cell_stages * healthy_cell_stages; i++){
        for(int j = 0; j < actions; j++){
            getline(f, line, ',');
            //cout << "[" <<line << "]" << endl;
            Q_values[i][j] = stod(line);
        }
        getline(f, line);
    }
    f.close();
}

void no_treatment(){
    cout << "No treatment" << endl;
    ScalarModel * model = new ScalarModel('a');
    model -> reset();
    for(int i = 350; i < 2000; i += 50){
        cout << "Time: " << i << " Healthy cells: " <<  HealthyCell::count << " Cancer cells: " << CancerCell::count << endl;
        model -> go(50);
    }
    delete model;
}

void low_treatment(char reward){
    cout << "Low treatment" << endl;
    ScalarModel * model = new ScalarModel(reward);
    double sum_scores = 0.0;
    for(int i = 0; i < 25; i++){
        model -> reset();
        double sum_r = 0;
        int count = 0;
        while (!model->inTerminalState()){
            //int obs = state();
            sum_r += model -> act(0);
            count++;
        }
        sum_scores += sum_r;
    }
    cout << "Average reward " << (sum_scores / 25.0) << endl;
    delete model;
}

void baseline_treatment(char reward){
    cout << "Baseline treatment" << endl;
    ScalarModel * model = new ScalarModel(reward);
    double sum_scores = 0.0;
    for(int i = 0; i < 25; i++){
        model -> reset();
        double sum_r = 0;
        int count = 0;
        while (!model->inTerminalState()){
            //int obs = state();
            sum_r += model -> act(1);
            count++;
        }
        sum_scores += sum_r;
    }
    cout << "Average reward " << (sum_scores / 25.0) << endl;
    delete model;
}

void eval_baseline(char reward, int count){
    cout << "Baseline treatment" << endl;
    ScalarModel * model = new ScalarModel(reward);
    int sum_length = 0;
    int squared_length = 0;
    int sum_fracs = 0;
    int squared_fracs = 0.0;
    int sum_doses = 0;
    int squared_doses = 0.0;
    int sum_w = 0;
    double sum_survival = 0.0;
    double squared_survival = 0.0;
    for (int i = 0; i < count; i++){
        model -> reset();
        int count_f = 0;
        int fracs = 0;
        int doses = 0;
        int time = 0;
        int init_hcell = HealthyCell::count;
        while (!model->inTerminalState()){
            int action = (count_f++ < 35)?1:1;
            model -> act(action);
            fracs++;
            doses += action + 1;
            time += 24;
        }
        sum_fracs += fracs;
        squared_fracs += fracs * fracs;
        sum_doses += doses;
        squared_doses += doses*doses;
        sum_length += time;
        squared_length += time*time;
        double survival = (double) HealthyCell::count / (double) init_hcell;
        sum_survival += survival;
        squared_survival += survival * survival;
        if (model -> end_type == 'W')
            sum_w++;
    }
    cout << "TCP: " << 100.0 * (double) sum_w / (double) count << endl;

    double mean_frac = (double) sum_fracs / (double) count;
    double std_frac = sqrt(((double) squared_fracs / (double) count) - (mean_frac * mean_frac));
    cout << "Average num of fractions: " << mean_frac << " std error: "<< std_frac <<endl;

    double mean_dose = (double) sum_doses / (double) count;
    double std_dose = sqrt(((double) squared_doses / (double) count) - (mean_dose * mean_dose));
    cout << "Average radiation dose: " << mean_dose << " std error: "<< std_dose <<endl;

    double mean_duration = (double) sum_length / (double) count;
    double std_duration = sqrt(((double) squared_length / (double) count) - (mean_duration * mean_duration));
    cout << "Average duration: " << mean_duration << " std error: "<< std_duration <<endl;

    double mean_survival = (double) sum_survival / (double) count;
    double std_survival = sqrt(((double) squared_survival / (double) count) - (mean_survival * mean_survival));
    cout << "Average survival: " << mean_survival << " std error: "<< std_survival <<endl;

    delete model;
}

void high_treatment(char reward){
    cout << "High treatment" << endl;
    ScalarModel * model = new ScalarModel(reward);
    double sum_scores = 0.0;
    for(int i = 0; i < 25; i++){
        model -> reset();
        double sum_r = 0;
        int count = 0;
        while (!model->inTerminalState()){
            //int obs = state();
            sum_r += model -> act(4);
            count++;
        }
        sum_scores += sum_r;
    }
    cout << "Average reward " << (sum_scores / 25.0) << endl;
    delete model;
}

void high_low_treatment(char reward){
    cout << "High low treatment" << endl;
    ScalarModel * model = new ScalarModel(reward);
    double sum_scores = 0.0;
    for(int i = 0; i < 25; i++){
        model -> reset();
        double sum_r = 0;
        int count = 0;
        while (!model->inTerminalState()){
            //int obs = state();
            if(count <= 3)
                sum_r += model -> act(3);
            else
                sum_r += model -> act(1);
            count++;
        }
        sum_scores += sum_r;
    }
    cout << "Average reward " << (sum_scores / 25.0) << endl;
    delete model;
}

void test_suite(char reward){
    low_treatment(reward);
    baseline_treatment(reward);
    high_treatment(reward);
    high_low_treatment(reward);
}

void TabularAgent::change_val(int state, int action, double val){
    Q_values[state][action] = val;
}


int main(int argc, char * argv[]){
    int n_epochs, cancer_cell_stages, healthy_cell_stages;
    char reward, state_type;
    if(argc == 1){
        n_epochs = 0;
        reward = 'd';
        state_type = 'i';
        cancer_cell_stages = 50;
        healthy_cell_stages = 5;
    }else{
        n_epochs = stoi(argv[1]);
        reward = argv[2][0];
        state_type = argv[3][0];
        cancer_cell_stages = stoi(argv[4]);
        healthy_cell_stages = stoi(argv[5]);
    }
    ScalarModel * model = new ScalarModel(reward);
    TabularAgent * agent = new TabularAgent(model, cancer_cell_stages, healthy_cell_stages, 5, state_type);
    if(argc == 8 && argv[7][0] == 'l'){
        agent -> load_Q(argv[6]);
    }
    if(argc == 1){
        for(int i = 0; i < 250; i++)
            agent -> change_val(i, 1, 1.0);
    }
    //agent -> run(n_epochs, 5000, 10, 0.8, 0.05, 0.8, 0.01, 0.99);
    //agent -> test(5, true, 0.99, false);
    agent -> test(1000, false, 0.99, true);
    agent -> treatment_var(1000);
    //agent -> save_Q(argv[6]);
    delete model;
    delete agent;
    return 0;
}



