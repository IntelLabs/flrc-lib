#include <iostream>
#include <fstream>
#include <map>
#include <string>

using namespace std;

int main(int argc, char *argv[]) {
    if(argc != 2) {
        cout << "Bad command line.\n";
        return -1;
    }
    ifstream inputfile(argv[1]);
    if(!inputfile) {
        cout << "Could not open file " << argv[1] << endl;
        return -1;
    }

    map<string,unsigned> func_map;
    unsigned total = 0;

    while(inputfile) {
        string func_name;
        inputfile >> func_name;
        if(inputfile.eof()) break;
        ++total;
        pair<map<string,unsigned>::iterator, bool> res = func_map.insert(pair<string,unsigned>(func_name,1));
        if(!res.second) {
            res.first->second++;
        }
    }

    multimap<unsigned,string> order;

    map<string,unsigned>::iterator iter;
    for(iter  = func_map.begin();
        iter != func_map.end();
      ++iter) {
        order.insert(pair<unsigned,string>(iter->second,iter->first));
    }

    cout << "Total " << total << " 1.0" << endl;

    multimap<unsigned,string>::reverse_iterator reviter;
    for(reviter  = order.rbegin();
        reviter != order.rend();
      ++reviter) {
        cout << reviter->second << " " << reviter->first << " " << (float)reviter->first / total << endl;
    }

    return 0;
}