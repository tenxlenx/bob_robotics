
#include "route_setup.h"
#include <thread>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

//! hash sequence matcher
class DTHW {

    private:
    HashMatrix m_long_sequence;
    std::deque<std::bitset<64>> m_short_sequence;
    std::deque<std::vector<int>> m_cost_matrix;
    std::deque<std::vector<long int>> m_accumulated_cost_matrix;
    std::vector<std::vector<int>> angle_dist_matrix;
    unsigned int m_roll_step;
    unsigned int m_current_sequence_size = 0;
    unsigned m_sequence_limit = 70;
    bool m_genP = true;
    int last_match_index = 0;
    int current_match;
    int norm_window_size = 100;
    int successful_sequence = 0;


    static void thread_stuff(std::bitset<64> sequence_element, std::vector<std::bitset<64>> curr_mat, int width, std::vector<int> &costMatRow, int norm_size) {
        auto hashvals = HashMatrix::calculateHashValues(sequence_element, curr_mat);
        std::vector<int> bestRotDists;
        costMatRow = HashMatrix::getBestHashRotationDists(hashvals , width,bestRotDists).first;
        HashMatrix::normalise_n(costMatRow,norm_size);
    }

    // calculate C matrix
    std::deque<std::vector<int>> calculate_cost_matrix(std::deque<std::bitset<64>> short_sequence, HashMatrix &h_matrix, bool normalise = true, int norm_window = 100) {
        auto curr_mat = h_matrix.getMatrix(); // get the current hash matrix bitset
        int width = h_matrix.getWidth();      // get the width of the matrix - as it's 1d and to know the mapping to 2d
        std::deque<std::vector<int>> costMatrix(short_sequence.size());

        // each element in the sequence will be processed by a thread
        std::vector<std::thread> threads;
        for (size_t i = 0; i < short_sequence.size(); i++) {
            std::thread thr(thread_stuff, short_sequence[i], std::ref(curr_mat), width, std::ref(costMatrix[i]), norm_window);
            threads.push_back(std::move(thr));
        }

        for (auto&& t : threads) {
            t.join();
        }

        m_cost_matrix = costMatrix;
        return costMatrix;
    }



    // calculate row distances
    std::pair<std::vector<int>, std::vector<int>> calculateNewRowDistances(std::bitset<64> last_hash_val, HashMatrix &matrix) {
        auto hashmat = matrix.getMatrix();
        std::vector<int> differenceMatrix = HashMatrix::calculateHashValues(last_hash_val,hashmat);

        std::vector<int> bestRotDists;
        std::pair<std::vector<int>, std::vector<int>> new_row = HashMatrix::getBestHashRotationDists(differenceMatrix, matrix.getWidth(),bestRotDists); // 1 row in cost mat

        return new_row;
    }

    // sequence logic
    std::pair<int,int> getBestSequence(std::deque<std::bitset<64>> short_sequence,
                                                    HashMatrix &hmat,
                                                    std::deque<std::vector<int>> &C,
                                                    std::deque<std::vector<long int>> &D
                                                   ) {

        std::vector<int> best_rots;
        if (D.empty() || m_genP) { // init D mat

             // init cost mat
            if (C.empty()) {
                C = calculate_cost_matrix(short_sequence, m_long_sequence, true, norm_window_size);
            }
            D = calculate_accumulated_cost_matrix(m_cost_matrix);
            successful_sequence = 0;
            m_genP = false;
        } else { // append row if exists

            auto new_row = calculateNewRowDistances(short_sequence.back(), hmat);
            best_rots = new_row.second;


            D = appendRowToD(new_row.first , C, D);
        }


        // calculate path
        auto P = calculateOptimalWarpingPath(D); // first is last match - what we want
        if (P[0].second == 0) { // 0 should(?) be error - very hacky
            m_genP = true;
        }
        auto ind_best_rot = 0;

        if (best_rots.size() > 0) {
            ind_best_rot = best_rots[ P[0].second ];
        }

        auto best_ind = P[0].second;

        return {best_ind, ind_best_rot};
    }

    //! appends a row to D ----  to debug
    std::deque<std::vector<long int>> appendRowToD(std::vector<int> cost_row, std::deque<std::vector<int>> &C, std::deque<std::vector<long int>> &D) {
        std::vector<long int> D_row(cost_row.size());

        HashMatrix::normalise_n(cost_row, norm_window_size);

        C.push_back(cost_row); // append C mat with  new cost row
        D.push_back(D_row); // push new empty row

        // copy first element of Cost matrix row
        int N = D.size()-1;
        int M = cost_row.size()-1;

        // add 1 to cum sum
        D[N][0] = D[N-1][0] + C[0][0];
        std::vector<long int> cum_sum(C.size());
        std::vector<long int> first_col;
         // cumulative sum of first column
        for  (size_t  i = 0; i < C.size(); i++) {
            first_col.push_back(C[i][0]);
        }
        std::partial_sum(first_col.begin(), first_col.end(), cum_sum.begin(), std::plus<int>());
        for (size_t i =0;  i < D.size(); i++) {
            D[i][0] = cum_sum[i];
        }


        for (int j = 1; j < M+1; j++) {
            auto up = D[N-1][j]; // up
            auto left = D[N][j-1]; // left
            auto upper_left = D[N-1][j-1]; // upper left
            std::vector<long int> squares({up,left,upper_left});
            auto min_val = *std::min_element( std::begin(squares), std::end(squares) );
            D.back()[j] = C.back()[j] + min_val;
        }
        m_accumulated_cost_matrix = D;
        m_cost_matrix = C;
        return D;
    }

    //! calculate D matrix
    std::deque<std::vector<long int>> calculate_accumulated_cost_matrix(std::deque<std::vector<int>> &C) {

        int N, M;
        N = C.size();
        M = C[0].size();

        std::deque<std::vector<long int>> D(C.size(),std::vector<long int>(C[0].size())); // accumulated cost matrix
        std::vector<int> cum_sum(C.size());
        std::vector<int> first_col;
         // cumulative sum of first column
        for  (size_t  i = 0; i < C.size(); i++) {
            first_col.push_back(C[i][0]);
        }
        std::partial_sum(first_col.begin(), first_col.end(), cum_sum.begin(), std::plus<int>());
        for (size_t i =0;  i < D.size(); i++) {
            D[i][0] = cum_sum[i];
        }

        // copy first row of Cost matrix
        for (size_t i = 0; i < D[0].size(); i++) {
            D[0][i] = C[0][i]; // row 0 of D = C
        }

        for (int i = 1; i < N; i++) {
            for (int j = 1; j < M; j++) {

                int up = D[i-1][j]; // up   D[i-2][j-1];
                int left = D[i][j-1]; // left    D[i-1][j-2];
                int upper_left = D[i-1][j-1]; // upper left


                std::vector<int> squares({up,left,upper_left});
                int min_val = *std::min_element( std::begin(squares), std::end(squares) );
                //D[n, m] = C[n, m] + min(D[n-1, m], D[n, m-1], D[n-1, m-1])
                D[i][j] = C[i][j] + min_val;
            }
        }
        m_accumulated_cost_matrix = D;
        return D;
    }

    //! calculate path
    std::vector<std::pair<int,int> > calculateOptimalWarpingPath(std::deque<std::vector<long int>> &D) {

        int N = D.size(); // row size
        int n = N -1;
        int m = -1;

        int min_index = 0;
        int minVal = 100000;
        auto curr_row = D[N-1];


        for (size_t i = 0; i < curr_row.size(); i++) {  // m = D[N - 1, :].argmin()
            if (curr_row[i] <= minVal) {
                minVal = curr_row[i];
                min_index = i;
            }
        }
        m = min_index; // best value of last
        std::vector<std::pair<int,int>> P; // path
        P.push_back({n,m}); // first node  add to vector

        while (n > 0) {
            std::pair<int,int> path_node;
            if ( m == 0) {
                path_node.first = n-1;
                path_node.second = 0;
            } else {
                std::vector<long int> squares({D[n-1][m-1],D[n-1][m],D[n][m-1]});
                int val = *std::min_element( std::begin(squares), std::end(squares) );

                if (val == D[n-1][m-1]) { // if upper left

                    path_node.first = n-1;
                    path_node.second = m-1;
                }
                else if ( val == D[n-1][m]) { // if up

                    path_node.first = n-1;
                    path_node.second = m;
                }
                else {
                     // if left
                    path_node.first = n;
                    path_node.second = m-1;
                }
            }
            P.push_back(path_node);
            n = path_node.first;
            m = path_node.second;
        }


        return P;
    }

    public:

    //! default constructor - init with a HashMatrix
    DTHW(HashMatrix long_sequence, int roll_step, int norm_w_size) {
        this->m_long_sequence = long_sequence;
        this->m_roll_step = roll_step;
        this->norm_window_size = norm_w_size;
    }

    //! add to sequence - if length reached, oldest element is removed and the function returns true
    bool addToShortSequence(std::bitset<64> hashValue,size_t sequence_size) {
        bool is_limit_reached = false;
        //m_sequence_limit = sequence_size;
        if (m_short_sequence.size() < sequence_size) {
            m_short_sequence.push_back(hashValue);
            is_limit_reached = false;
        } else {
            m_short_sequence.pop_front();
            m_short_sequence.push_back(hashValue);
            is_limit_reached = true;
        }
        m_current_sequence_size = m_short_sequence.size();
        return is_limit_reached;
    }

    // get best match
    std::pair<int, int> getBestMatch() {

        if (m_current_sequence_size >= m_sequence_limit) {

            auto P = getBestSequence(m_short_sequence, m_long_sequence, m_cost_matrix, m_accumulated_cost_matrix );
            int match_index =  P.first;  // get best match index
            last_match_index = match_index;


            current_match = match_index;
            return {match_index, P.second};
        }

        std::vector<int> differenceMatrix = HashMatrix::calculateHashValues(m_short_sequence.back(),m_long_sequence.getMatrix());
        int min_col, min_row, min_value;

        HashMatrix::argmin_matrix(differenceMatrix , m_roll_step, min_col, min_row, min_value) ;
        last_match_index = min_row;
        this->current_match = min_row;
        return {min_row, min_col};

    }

    int getCurrentMatch() { return current_match; }

    cv::Mat getCostMatrix() {
        // Create a new, _empty_ cv::Mat with the row size of OrigSamples
        cv::Mat costMat(0, m_cost_matrix.size(), cv::DataType<int>::type);

        for (unsigned int i = 0; i < m_cost_matrix.size(); ++i) {
            // Make a temporary cv::Mat row and add to NewSamples _without_ data copy
            cv::Mat Sample(1, m_cost_matrix[i].size(), cv::DataType<int>::type, m_cost_matrix[i].data());

            costMat.push_back(Sample);
        }
        return costMat;
    }
};

//! checks scores based on frame distance, positional distance, and angular error
std::pair<meter_t,degree_t> scoring(Route &testRoute, int current_frame, Route &referenceRoute, int match_index, units::angle::degree_t rotation) {
    auto currentNode = testRoute.nodes[current_frame]; // the current node in the testted route
    auto referenceNode = referenceRoute.nodes[match_index]; // matched node in the reference route - single match
    std::pair<millimeter_t, degree_t> errors;

    meter_t dist = RouteNode::distance(currentNode, referenceNode); // distance from the test node in mm - single match
    degree_t angDiff = RouteNode::angle_distance(currentNode, referenceNode, rotation); // angle  angular difference - single match

    errors.first = dist;
    errors.second = angDiff;

    return errors;
}


cv::Mat show_angle_matrix(std::vector<std::vector<int>> dist_mat) {

    cv::Mat costMat(0, dist_mat.size(), cv::DataType<int>::type);

    for (unsigned int i = 0; i < dist_mat.size(); ++i) {
        // Make a temporary cv::Mat row and add to NewSamples _without_ data copy
        cv::Mat Sample(1, dist_mat[i].size(), cv::DataType<int>::type, dist_mat[i].data());

        costMat.push_back(Sample);
    }

    cv::normalize(costMat, costMat, 0, 255, cv::NORM_MINMAX);
    costMat.convertTo(costMat,CV_8UC1);
    applyColorMap(costMat, costMat, cv::COLORMAP_JET);
    return costMat;
}

//---------------------------------- main  --------------------------------------///
int main(int argc, char **argv) {

    bool show_images = true;    // show visual
    int seq_length = 70;        // sequence length
    int roll_step = 180;         // number of rotations for a view
    cv::Size unwrapRes(360,90); // resolution of the unwrrapped video
    bool createVideo = false;    // if true, it saves unwrapped video
    bool unwrap = false;         // if true, videos will be unwrapped
    int skipstep = 2;           // skip frames in training matrix
    int testRoute_skipstep =2;
    int num_datasets = 2;
    int testRouteNum = 0;

    std::vector<Route> route_vector;
    std::vector<DTHW> sequence_matcher_vector;

    int current_step;
    for (int i = 0; i < num_datasets;  i++) {

        if (i == testRouteNum) {
            current_step = testRoute_skipstep;
        } else {
            current_step = skipstep;
        }

        auto route = Route(i,roll_step, current_step, unwrap, createVideo, unwrapRes);
        std::cout << " route " << i << " creating Hash matrix" << std::endl;
        HashMatrix hashMat(route.nodes, roll_step);
        route.set_hash_matrix(hashMat);
        route_vector.push_back(route);
        DTHW sequence_matcher(route_vector[i].getHashMatrix(), roll_step, 100); // init sequence matcher with training matrices
        sequence_matcher_vector.push_back(sequence_matcher);
    }
    std::cout << " all routes are initialized" << std::endl;

    // set test route
    std::vector<RouteNode> testRoute = route_vector[testRouteNum].nodes;

    cv::Mat prev_costMat, curr_costMat;

    // simulation - for all nodes of test route - get best match
    for (size_t h = 0; h < testRoute.size(); h++) {
        auto hash = testRoute[h].image_hash; // current hash of test set

        std::vector<cv::Mat> img_rots(roll_step); // need to make it with fix size
        cv::Mat img_gray;
        cv::cvtColor(testRoute[h].image, img_gray, cv::COLOR_BGRA2GRAY);
        std::vector<std::bitset<64>> hash_rots = HashMatrix::getHashRotations(img_gray ,roll_step, img_rots);



        int min_value; // best value

        // for all datasets calculate sequence
        for (int j = 0; j < sequence_matcher_vector.size(); j++) {
            if (j == testRouteNum) {
                continue;
            }
            sequence_matcher_vector[j].addToShortSequence(hash,seq_length);
        }
        // get the best matching frame using sequence matching
        if (show_images) {
            cv::Mat conc_img1, conc_img2;
            std::vector<cv::Mat> concat_imgs({testRoute[h].image});

            for (int i = 0; i < route_vector.size(); i++ ) {
                if (i == testRouteNum) {
                    continue;
                }

                // can change ordering here << ang matrix >>
                std::vector<std::vector<int>> ang_distances;

                int rot_size = hash_rots.size();
                int half_rot = rot_size/ 2;

                for (int rh = half_rot; rh < rot_size; rh++) {
                    std::vector<int> row_distances;
                    for (RouteNode route_node : route_vector[i].nodes) {
                        auto img_hash = route_node.image_hash;
                        auto dist = DCTHash::distance(hash_rots[rh], img_hash);
                        row_distances.push_back(dist);
                    }
                    ang_distances.push_back(row_distances);
                }
                for (int rh = 0; rh < half_rot; rh++) {
                    std::vector<int> row_distances;
                    for (RouteNode route_node : route_vector[i].nodes) {
                        auto img_hash = route_node.image_hash;
                        auto dist = DCTHash::distance(hash_rots[rh], img_hash);
                        row_distances.push_back(dist);
                    }
                    ang_distances.push_back(row_distances);
                }


                prev_costMat = show_angle_matrix(ang_distances);

                cv::imshow("angle_dist_mat", prev_costMat);






                // pixel diff
               // std::pair<int,int> pixel_match = Route::pixel_distance_matrix(img_gray,route_vector[i] ,roll_step);
               // auto pixel_index = pixel_match.first;
                int pixel_index = 0;


                auto seq_index = sequence_matcher_vector[i].getBestMatch();
                int min_value;
                auto single_match = HashMatrix::getSingleMatch(hash, route_vector[i].getHashMatrix(),min_value,roll_step );
                std::vector<int> differenceMatrix = HashMatrix::calculateHashValues(hash,route_vector[i].getHashMatrix().getMatrix());
                //cv::Mat s_dist_mat(differenceMatrix);
                //cv::imshow("diff",differenceMatrix);

                cv::Mat dist_mat1;
                cv::Mat dist_mat = sequence_matcher_vector[i].getCostMatrix();
                cv::Mat image_to_rotate = route_vector[i].nodes[seq_index.first].image;
                concat_imgs.push_back(route_vector[i].nodes[single_match.second].image);
                concat_imgs.push_back(image_to_rotate);

                int pixel_step = int(360.0 / (float)roll_step);
                int single_match_angle = single_match.second * pixel_step;
                int seq_match_angle = seq_index.second * pixel_step;

                int seq_angle;
                if (seq_match_angle < 180) {
                    seq_angle = seq_match_angle;
                } else {
                    seq_angle = seq_match_angle -360;
                }

                auto seq_errors = scoring(route_vector[testRouteNum], h, route_vector[i], seq_index.first,degree_t(seq_angle));
                std::cout << " pos error= " << seq_errors.first << "  angle to turn = " << seq_angle << std::endl;

                //std::cout << " single : " << single_match.second <<" " << " seq " << seq_index.first << " " << "pixel " << pixel_index  << std::endl;

                if (dist_mat.size().height > 0) {
                    //
                    //dist_mat*=10;
                    cv::normalize(dist_mat, dist_mat1, 0, 255, cv::NORM_MINMAX);
                    dist_mat1.convertTo(dist_mat1,CV_8UC1);
                    //applyColorMap(dist_mat1, dist_mat1, cv::COLORMAP_JET);
                    //cv::imshow("dist_mat", dist_mat1);
                }

            }


            cv::vconcat(concat_imgs, conc_img1);
            cv::resize(conc_img1, conc_img2, cv::Size(), 1, 1);
            cv::imshow("testing single vs seq", conc_img2);
            cv::waitKey(1);

        }
    }
    cv::waitKey(5000);

    return 0;
}


