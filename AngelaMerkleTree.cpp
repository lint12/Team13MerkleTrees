#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <queue>
#include <atomic>
#include <cmath>
#include <stack>
#include <algorithm>
#include <mutex>
#include <thread>
#include <ctime>

using namespace std;

class LeafNode {
public:
  string val;
  string hash;

  LeafNode(string _val, string _hash);
};


LeafNode::LeafNode(string _val, string _hash) {
  val = _val;
  hash = _hash;
}

class Transaction {
public:
  string val;
  int index;

  Transaction(string _val, int _index);
};


Transaction::Transaction(string _val, int _index) {
  val = _val;
  index = _index;
}

class MerkleNode {
public:
  string hash;
  string encoding; // Needed>
  LeafNode* leaf;
  MerkleNode* left;
  MerkleNode* right;
  mutex mtx;
  int visited;

  MerkleNode(string _hash, string _encoding, LeafNode* _leaf);
  MerkleNode(string _hash, LeafNode* _leaf);
  MerkleNode(string hash);
  MerkleNode();
};

MerkleNode::MerkleNode(string _hash, string _encoding, LeafNode* _leaf) {
  hash = _hash;
  leaf = _leaf;
  encoding = _encoding;
  visited = 0;
  left = NULL;
  right = NULL;
}

MerkleNode::MerkleNode(string _hash, LeafNode* _leaf) {
  hash = _hash;
  leaf = _leaf;
  encoding = "-1";
  visited = 0;
  left = NULL;
  right = NULL;
}

MerkleNode::MerkleNode(string _hash) {
  hash = _hash;
  leaf = NULL;
  encoding = "-1";
  visited = 0;
  left = NULL;
  right = NULL;
}

MerkleNode::MerkleNode() {
  hash = -1;
  leaf = NULL;
  encoding = "-1";
  left = NULL;
  right = NULL;
}

class Proof {
public:
  string val;
  string hash;
  vector<MerkleNode*> siblingHashes;

  Proof(string _val, string _hash, vector<MerkleNode*> _siblingHashes);
};

Proof::Proof(string _val, string _hash, vector<MerkleNode*> _siblingHashes) {
  val = _val;
  hash = _hash;
  siblingHashes = _siblingHashes;
}

string findEncoding(int index, int depth) {
  if (depth == 0)
    return "-1";

  vector<int> encode(depth);

  for (int i = 0; index > 0; i++) {
    encode.at(i) = index % 2;
    index = index / 2;
  }

  stringstream ss;
  for (int i = encode.size() - 1; i > -1; i--)
    ss << encode.at(i);
  return ss.str();
}

// Helper function for populate()
MerkleNode* recursivePopulate(vector<MerkleNode*> hashedNodes, hash<string> hash, int depth) {
  vector<MerkleNode*> upperLevel;
  if(hashedNodes.size() == 1){
    hashedNodes.at(0)->encoding = "-1";
    return hashedNodes.at(0);
  }

  for (int i = 0; i < hashedNodes.size() - 1; i = i + 2) {

    stringstream ss;
    ss << (hashedNodes.at(i))->hash << (hashedNodes.at(i+1))->hash;
    stringstream output(ss.str());

    size_t temp = hash(ss.str());
    ss.str("");
    ss << temp;
    MerkleNode* node = new MerkleNode(ss.str());

    node->left = hashedNodes.at(i);
    node->right = hashedNodes.at(i+1);
    node->encoding = findEncoding(i / 2, depth);

    upperLevel.push_back(node);
  }

  // Is this needed now?
  if(hashedNodes.size() % 2 == 1) {
    stringstream ss;
    size_t temp = hash(hashedNodes.at(hashedNodes.size()-1)->hash);
    ss << temp;
    MerkleNode* node = new MerkleNode(ss.str());

    node->left = hashedNodes.at(hashedNodes.size()-1);
    node->encoding = findEncoding((hashedNodes.size() - 1) / 2, depth);
    upperLevel.push_back(node);
  }

  return recursivePopulate(upperLevel, hash, depth - 1);
}

// Returns root of merkle tree
MerkleNode* populate(vector<LeafNode> leaves) {
  vector<MerkleNode*> base;
  hash<string> hash;
  int depth = 23; // Change for testing/scaling

  for (int i = 0; i < leaves.size(); i++) {
    MerkleNode* node = new MerkleNode(leaves.at(i).hash, findEncoding(i, depth), &leaves.at(i));

    base.push_back(node);
  }

  return recursivePopulate(base, hash, depth - 1); // Depth of tree
}


class MerkleTree {
public:
  atomic<MerkleNode*> root;
  vector<string> conflicts;

  MerkleTree();
  bool insert_leaf(int index, string data);
  MerkleNode* get_signed_root();
  Proof* generate_proof(int index);
  bool verify_proof(Proof* proof, string data, MerkleNode* root);
  void batch_update(vector<Transaction> trans, int ind);
  vector<string> find_conflicts(vector<Transaction> trans);
  void update(Transaction trans);
};

MerkleTree::MerkleTree() {
  vector<LeafNode> init;

  hash<string> hash;
  stringstream convert;
  convert << hash("");
  string hashedValue = convert.str();

  for (int i = 0; i < pow(2, 23); i++) {
    LeafNode temp("", hashedValue); // Whatever # of nodes here
    init.push_back(temp);
  }

  root.store(populate(init));
}

bool MerkleTree::insert_leaf(int index, string data) {
  int last = pow(2, 23) - 1; // Change on scaling
  int first = 0;
  int middle = 0;
  int depth = 0; // Depth needs to be check to make sure it's right level

  MerkleNode* temp = root.load();
  hash<string> hash;
  stack<MerkleNode*> siblings;
  stack<MerkleNode*> parents;

  while (first <= last) {
    middle = (first + last) / 2;
    if (middle < index) {
      first = middle + 1;

      parents.push(temp);
      siblings.push(temp->left);
      temp = temp->right;

      depth++;
    } else if (middle > index) {
      last = middle - 1;

      parents.push(temp);
      siblings.push(temp->right);
      temp = temp->left;

      depth++;
    } else {
      if(depth != 23) { // NEED to change when scaling up
        parents.push(temp);
        siblings.push(temp->right);
        temp = temp->left;

        while (temp->right) {
          parents.push(temp);
          siblings.push(temp->left);
          temp = temp->right;
        }
      }

      size_t hashed = hash(data);
      stringstream ss;
      ss << hashed;
      LeafNode* leaf = new LeafNode(data, ss.str());

      temp->hash = ss.str();
      temp->leaf = leaf;

      // Traverse back up
      while (!parents.empty()) {
        MerkleNode* head = parents.top();

        stringstream ss;
        ss << temp->hash << siblings.top()->hash;
        stringstream output(ss.str());

        size_t hashedTemp = hash(ss.str());
        ss.str("");
        ss << hashedTemp;

        head->hash = ss.str();
        parents.pop();
        siblings.pop();
        temp = head;
      }
      return 1;
    }
  }

  // Above hash values must be updated

  return 0;
}

MerkleNode* MerkleTree::get_signed_root() {
  return root.load();
}

Proof* MerkleTree::generate_proof(int index) {
  int last = pow(2, 23) - 1;
  int first = 0;
  int middle = 0;
  int depth = 0; // Depth needs to be check to make sure it's right level

  MerkleNode* temp = root.load();
  hash<string> hash;
  stack<MerkleNode*> siblings;

  cout << "Before while" << endl;

  while (first <= last) {
    middle = (first + last) / 2;
    if (middle < index) {
      first = middle + 1;
      siblings.push(temp->left);
      temp = temp->right;
      depth++;
    } else if (middle > index) {
      last = middle - 1;
      siblings.push(temp->right);
      temp = temp->left;
      depth++;
    } else {
      if(depth != 23) { // NEED to change when scaling up
        siblings.push(temp->right);
        temp = temp->left;

        while (temp->right) {
          siblings.push(temp->left);
          temp = temp->right;
        }
      }

      cout << "After BS" << endl;

      vector<MerkleNode*> proofHashes;
      while (!siblings.empty()) {
        MerkleNode* temp = siblings.top();
        proofHashes.push_back(temp);
        siblings.pop();
      }

      stringstream ss;
      ss <<  hash("");

      cout << "About to Return" << endl;
      cout << temp->hash << endl;
      if (temp->hash.compare(ss.str()) == 0)
        return NULL;
      Proof* result = new Proof(temp->leaf->val, temp->hash, proofHashes);
      cout << "returned" << endl;
      return result;
    }
  }
  return NULL;
}

bool MerkleTree::verify_proof(Proof* proof, string data, MerkleNode* root) {
  if (proof == NULL)
    return 0;

  hash<string> hash;
  size_t hashedData = hash(data);
  int size;
  size_t newHash;
  stringstream ss;

  cout << "Proof is: " << proof->val << endl;

  if (proof) {
    size = proof->siblingHashes.size();
    ss << hashedData << proof->siblingHashes.at(0)->hash;
    newHash = hash(ss.str());
    ss.str("");

    for (int i = 1; i < size; i++) {
      ss << newHash << proof->siblingHashes.at(i)->hash;
      newHash = hash(ss.str());
      ss.str("");
    }

    ss << newHash;

    return (ss.str().compare(root->hash) == 0);
  }

  return false;
}

vector<string> MerkleTree::find_conflicts(vector<Transaction> trans) {
  int depth = 23;
  vector<string> encoded;
  vector<string> conflicts;
  stringstream ss;


  for (int i = 0; i < trans.size(); i++) {
    string encoding = findEncoding(trans.at(i).index, depth);
    cout << encoding << " ";
    encoded.push_back(encoding);
  }
  cout << endl;


  for (int i = 0; i < encoded.size() - 1; i++) {
    for (int j = i + 1; j < encoded.size(); j++){

      for (int z = 0; z < depth; z++){
        if (encoded.at(i).substr(z, 1).compare(encoded.at(j).substr(z, 1)) == 0)
          ss << encoded.at(i).substr(z, 1);
        else
          break;
      }

      string potentialConflict = ss.str();
      if (potentialConflict.empty()) {
        potentialConflict = "-1";
      }
      if (!(find(conflicts.begin(), conflicts.end(), potentialConflict) != conflicts.end())) {
        conflicts.push_back(potentialConflict);
      }
      ss.str("");
    }
  }
  return conflicts;
}

void MerkleTree::batch_update(vector<Transaction> trans, int ind) {
  this->conflicts = find_conflicts(trans);
  for (int i = ind; i < trans.size(); i++)
    update(trans.at(i));
}

void MerkleTree::update(Transaction trans) {
  int last = pow(2, 23) - 1;
  int first = 0;
  int middle = 0;
  int depth = 0; // Depth needs to be check to make sure it's right level
  bool isInConflict = false;

  MerkleNode* temp = root.load();
  MerkleNode* prev = temp;

  while (first <= last) {
    middle = (first + last) / 2;
    if (middle < trans.index) {
      first = middle + 1;
      prev = temp;
      temp = temp->right;
      depth++;
    } else if (middle > trans.index) {
      last = middle - 1;
      prev = temp;
      temp = temp->left;
      depth++;
    } else {
      if(depth != 23) { // NEED to change when scaling up
        prev = temp;
        temp = temp->left;

        while (temp->right) {
          prev = temp;
          temp = temp->right;
        }
      }
      break;
    }
  }

  for (int i = 0; i < this->conflicts.size(); i++) {
    if (prev->encoding.compare(this->conflicts.at(i)) == 0)
      isInConflict = true;
  }

  if (isInConflict) {
    prev->mtx.lock();
    if (prev->visited == 0) {
      prev->visited = 1;
      //remember to kill the thread rip
    }
    else {
      insert_leaf(trans.index, trans.val);
    }
    prev->mtx.unlock();
  }
  else { //parent is not a conflict then process that transaction
    insert_leaf(trans.index, trans.val);
  }

  return;
}

void run(int randNum[4], int id, MerkleTree *tree, vector<Transaction> trans, int st) {
  Proof* proof;
  bool res = false;

  cout << "HI IM IN RUN" << endl;
  for (int i = 0; i < 4; i++) {
    if (randNum[i] == 0) {
      cout << "Thread: " << id << " trying to insert leaf" << endl;
      tree->insert_leaf(trans.at(i).index, trans.at(i).val);
    }
    else if (randNum[i] == 1) {
      cout << "Thread: " << id << " trying to get signed root" << endl;
      tree->get_signed_root();
    }
    else if (randNum[i] == 2) {
      cout << "Thread: " << id << " trying to generate proof" << endl;
      proof = tree->generate_proof(trans.at(i).index);
      cout << "Thread: " << id << " trying to verify proof" << endl;
      res = tree->verify_proof(proof, trans.at(i).val, tree->get_signed_root());
    }
    else if (randNum[i] == 3) {
      cout << "Thread: " << id << " trying to batch update" << endl;
      tree->batch_update(trans, st);
    }
  }
}

int main() {
  vector<thread> threads;
  int randNum[4];
  srand (time(NULL));
  int st = 0;

  for (int i = 0; i < 4; i++) {
    randNum[i] = rand() % 4;
  }

  // Create list of nodes with precreated hashes through hash<T>
  // populate Merkle Tree with list
  hash<string> test;

  string A = "A";
  string B = "B";
  string C = "C";

  stringstream convert;
  convert << test(A);
  string hashA = convert.str();

  convert.str("");
  convert << test(B);
  string hashB = convert.str();

  convert.str("");
  convert << test(C);
  string hashC = convert.str();

  LeafNode tempA("1", hashA);
  LeafNode tempB("2", hashB);
  LeafNode tempC("3", hashC);

  vector<LeafNode> result;
  result.push_back(tempA);
  result.push_back(tempB);
  result.push_back(tempA);
  result.push_back(tempB);
  result.push_back(tempA);
  result.push_back(tempB);
  result.push_back(tempA);
  result.push_back(tempB);

  vector<LeafNode> tester;
  tester.push_back(tempA);
  tester.push_back(tempB);
  tester.push_back(tempC);

  MerkleNode* root = populate(result);

  cout << "D: Before MerkleTree" << endl;
  MerkleTree tree;
  tree.insert_leaf(0, "123");

  vector<Transaction> trans;
  Transaction tA("I", 0);
  Transaction tB("hate", 3);
  Transaction tC("this", 7);
  Transaction tD("ish", 5);
  Transaction tE("hi", 2);

  trans.push_back(tA);
  trans.push_back(tB);
  trans.push_back(tC);
  trans.push_back(tD);
  trans.push_back(tE);

  vector<string> returned;
  returned = tree.find_conflicts(trans);
  for (int i = 0; i < returned.size(); i++)
    cout << "Conflict (" << i << "): " << returned.at(i) << endl;

//run the threads here
  for (int tnum = 0; tnum < 32; tnum++)
  {
    threads.push_back(thread(run, randNum, tnum, &tree, trans, st));
    st++;
  }

  for_each(threads.begin(), threads.end(), mem_fn(&thread::join));

  /*
  MerkleNode* treeNode = tree.get_signed_root();
  while(treeNode->left->left){
    treeNode = treeNode->left;
  }

  cout << "Encoding the first two siblings: " << root->encoding << " " << root->left->encoding<< " " << root->right->encoding << " " << root->right->right->left->encoding << endl;

  cout << "First Hash: " << treeNode->left->hash << " Second Hash: " << treeNode->right->hash << endl;
  cout << "Head hash: " << treeNode->hash << endl;
  cout << "MerkleRoot: " << tree.get_signed_root()->hash << endl;
  tree.insert_leaf(1, "777");
  cout << "First Hash: " << treeNode->left->hash << " Second Hash: " << treeNode->right->hash << endl;
  cout << "Head hash: " << treeNode->hash << endl;
  cout << "MerkleRoot: " << tree.get_signed_root()->hash << endl;

  Proof* pro = tree.generate_proof(1);
  cout << "Proof values, val: " << pro->val << " Hash: " << pro->hash << endl;
  cout << "Proof Compare: " << tree.verify_proof(pro, "777", tree.get_signed_root()) << endl;*/
}
