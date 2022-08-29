#ifndef CORECOMMON_SRC_BTREE_HPP_
#define CORECOMMON_SRC_BTREE_HPP_

#include <memory>
#include <optional>

#include "util.hpp"

template<class K, class V>
struct Node {
	struct Iterator {
		using iterator_category = std::input_iterator_tag;
		using difference_type = void;
		using value_type = Node;
		using pointer = Node*;

		Node* root_p;
		Node* current;

		void operator++() {
			if (current==root_p) {
				return;
			} else if (current->right) {
				current = current->right.get();
				while (current->left) current = current->left.get();
			} else if (current->parent->left.get()==current) {
				current = current->parent;
			} else {
				while (current->parent!=root_p && current->parent->right.get()==current) current = current->parent;
				current = current->parent;
			}
		}
//
//		void operator--() {
//			if (current==root_p) {
//				while (current->right) current=current->right.get();
//			} else if (current->parent->right.get()==current) {
//				current=current->parent;
//			} else if (current->left) {
//				current = current->left;
//				while (current->right) current = current->right.get();
//			} else {
//				while (current->parent!=root_p && current->parent->left.get()==current) current = current->parent;
//				current = current->parent;
//			}
//		}

		bool operator==(Iterator const& other) const {
			return other.current==current;
		}

		bool operator!=(Iterator const& other) const {
			return other.current!=current;
		}

		Node& operator*() {
			return *current;
		}

		Node* operator->() {
			return current;
		}
	};

	struct Root {
		std::unique_ptr<Node> ptr;

		Node* find(K const& fx) {
			return ptr ? ptr->find(fx) : nullptr;
		}

		void insert(K&& ix, V&& iv) {
			if (ptr) ptr->insert(std::move(ix), std::move(iv));
			else ptr = std::make_unique<Node>(std::move(ix), std::move(iv));
		}

		Iterator begin() {
			Iterator iter {.current=ptr.get(), .root_p=nullptr};
			while (iter.current && iter.current->left) iter.current = iter.current->left.get();
			return iter;
		}

		Iterator end() {
			return {.current=ptr.get(), .root_p=nullptr};
		}
	};

	Node* parent;
	Root* root;

	char h_diff;

	K x;
	V v;

	std::unique_ptr<Node> left;
	std::unique_ptr<Node> right;

	Node(Root* parent, K&& k, V&& v): root(parent), parent(nullptr), h_diff(0), x(std::move(k)), v(std::move(v)) {}
	Node(Node* parent, K&& k, V&& v): root(nullptr), parent(parent), h_diff(0), x(std::move(k)), v(std::move(v)) {}

	std::unique_ptr<Node>* parent_ptr() {
		if (parent) return *parent->left.get()==this ? &parent->left : &parent->right;
		else if (root) return &root->ptr;
		else return nullptr;
	}

	Node* rot_right() {
		std::unique_ptr<Node>& ptr = *parent_ptr();
		ptr.release();
		ptr.swap(right);
		right.swap(ptr->left);
		ptr->left=std::unique_ptr(this);
		
		ptr->parent = parent;
		parent = *ptr;

		if (ptr->h_diff<=0) h_diff--;
		else h_diff -= ptr->h_diff+1;

		if (h_diff>=0) ptr->h_diff--;
		else ptr->h_diff += h_diff-1;

		return *ptr;
	}

	Node* rot_left() {
		std::unique_ptr<Node>& ptr = *parent_ptr();

		ptr.release();
		ptr.swap(left);
		left.swap(ptr->right);
		ptr->right=std::unique_ptr(this);

		ptr->parent = parent;
		parent = *ptr;

		if (ptr->h_diff>=0) h_diff++;
		else h_diff -= ptr->h_diff-1;

		if (h_diff<=0) ptr->h_diff++;
		else ptr->h_diff += h_diff+1;

		return ptr;
	}

	Node* rebalance(bool r_inc) {
		Node* node = this;

		while (node) {
			bool is_right = node->parent && node->parent->right.get()==node;

			if (r_inc) {
				node->h_diff++;

				if (node->h_diff>=2) {
					if (node->right->h_diff==-1) node->right->rot_left();
					node=node->rot_right();

					if (node->h_diff==0) r_inc = is_right;
					else break;
				} else if (node->h_diff>0) {
					r_inc = is_right;
				}
			} else {
				node->h_diff--;

				if (node->h_diff<=-2) {
					if (node->left->h_diff==1) node->left->rot_right();
					node=node->rot_left();

					if (node->h_diff==0) r_inc = is_right;
					else break;
				} else if (node->h_diff<0) {
					r_inc = is_right;
				}
			}

			node=node->parent;
		}

		return node;
	}

	Node* find(K const& fx) {
		Node* node = this;

		while (true) {
			if (fx<node->x && node->left) {
				node = node->left.get();
			} else if (fx>node->x && node->right) {
				node = node->right.get();
			} else {
				break;
			}
		}

		return node;
	}

	void insert(K&& ix, V&& iv) {
		Node* res = find(ix);

		if (!res->parent || ix<res->x) {
			res->left = std::make_unique<Node>(res, std::move(ix), std::move(iv));
			res->left->parent = res;

			res->rebalance(false);
		} else if (ix>res->x || (ix==res->x && !res->right)) {
			res->right = std::make_unique<Node>(res, std::move(ix), std::move(iv));
			res->right->parent = res;

			res->rebalance(true);
		} else {
			res->right->insert(std::move(ix), std::move(iv));
		}
	}

	void remove() {
		if (root) {
			root->ptr.release();
			return;
		}

		bool is_right = this==parent->right.get();
		std::unique_ptr<Node>& ptr = is_right ? parent->right : parent->left;

		if (!right || !left) {
			rebalance(parent, !is_right);
		}

		if (left && !right) {
			ptr.swap(std::unique_ptr<Node>(left.release()));
		} else if (right && !left) {
			ptr.swap(std::unique_ptr<Node>(right.release()));
		} else if (left && right) {
			std::unique_ptr<Node>& rl = right;
			while (rl->left) rl = rl->left;

			//i dont know why this is so convoluted. who doesnt like swapping?!?/
			std::unique_ptr<Node> rl_cpy(rl.release());
			std::unique_ptr<Node> prev_r;
			if (rl_cpy!=right) prev_r.swap(right);

			if (rl_cpy->right) rl.swap(rl_cpy->right);

			rl_cpy->left.swap(left);
			Node* prev_parent = rl_cpy->parent;
			rl_cpy->parent = parent;
			rl_cpy->h_diff = h_diff;
			rl_cpy->right.swap(prev_r);

			rebalance(prev_parent, rl_cpy!=right);
			ptr.swap(rl_cpy);
		} else {
			ptr.reset();
		}
	}

	Iterator begin() {
		Iterator iter {.current=this, .root_p=parent};
		while (iter.current->left) iter.current = iter.current->left.get();
		return iter;
	}

	Iterator end() {
		return {.current=parent, .root_p=parent};
	}

//	Node merge(Node* other) {
//		Node new_root;
//		for (Iterator iter = begin(); )
//	}
};

#endif