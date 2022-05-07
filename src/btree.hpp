#ifndef CORECOMMON_SRC_BTREE_HPP_
#define CORECOMMON_SRC_BTREE_HPP_

//ur basic ass AVL tree or something i dont even know, just doing things
template<class X>
class BTree {
	enum class State {
		EqHeight,
		RightLarger
	};

	State x;
	BTree* l, *r;

};

#endif //CORECOMMON_SRC_BTREE_HPP_
