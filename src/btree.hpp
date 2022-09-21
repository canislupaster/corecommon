#ifndef CORECOMMON_SRC_BTREE_HPP_
#define CORECOMMON_SRC_BTREE_HPP_

#include <memory>
#include <optional>
#include <cassert>

#include "util.hpp"

template<class K, class V>
struct Node {
	struct Iterator {
		using iterator_category = std::input_iterator_tag;
		using difference_type = void;
		using value_type = Node;
		using pointer = Node*;

		Node* begin, *end;
		Node* current;

		void operator++() {
			if (current==end) {
				return;
			} else if (current->right) {
				current = current->right.get();
				while (current->left) current = current->left.get();
			} else {
				while (current->parent!=end && current->parent->right.get()==current) current = current->parent;
				current = current->parent;
			}
		}

		Iterator operator+(unsigned i) const {
			Iterator ret = *this;
			while ((i--)>0) ++ret;
			return ret;
		}

		std::unique_ptr<Node> consume() {
			std::unique_ptr<Node>& ptr = *current->parent_ptr();
			Node* ptr_p = current->parent;
			Root* root_p = current->root;

			this->operator++();
			if (ptr->left) ptr->left.reset();

			std::unique_ptr<Node> ret = std::unique_ptr<Node>(ptr.release());
			ptr.swap(ret->right);

			if (ptr) {
				ptr->parent = ptr_p;
				ptr->root = root_p;
			}

			ret->h_diff = 0;

			return ret;
		}

		void operator--() {
			if (current==end) {
				current = begin;
				while (current->right) current=current->right.get();
			} else if (current->left) {
				current = current->left.get();
				while (current->right) current = current->right.get();
			} else {
				while (current->parent!=end && current->parent->left.get()==current) current = current->parent;
				current = current->parent;
			}
		}

		Iterator operator-(unsigned i) const {
			Iterator ret = *this;
			while ((i--)>0) --ret;
			return ret;
		}

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

		template<class Compare=std::less<K>>
		Node* find(K const& fx, Compare cmp={}) {
			return ptr ? ptr->find(fx, cmp) : nullptr;
		}

		template<class Compare=std::less<K>>
		void insert_node(std::unique_ptr<Node>&& node, Compare cmp={}) {
			if (ptr) {
				node->root = nullptr;
				ptr->insert_node(std::move(node), cmp);
			} else {
				node->root = this;
				node->parent = nullptr;
				ptr.swap(node);
			}
		}

		template<class Compare=std::less<K>>
		Node* insert(K&& ix, V&& iv, Compare ins_cmp={}) {
			if (ptr) return ptr->insert(std::move(ix), std::move(iv), ins_cmp);
			else {
				ptr = std::make_unique<Node>(this, std::move(ix), std::move(iv));
				return ptr.get();
			}
		}

		Iterator begin() {
			Iterator iter {.current=ptr.get(), .begin=ptr.get(), .end=nullptr};
			while (iter.current && iter.current->left) iter.current = iter.current->left.get();
			return iter;
		}

		Iterator end() {
			return {.current=nullptr, .begin=ptr.get(), .end=nullptr};
		}

		Iterator iter_ref(Node* ref) {
			return {.current=ref, .begin=ptr.get(), .end=nullptr};
		}

		void swap(Root& other) {
			ptr.swap(other.ptr);
			if (ptr) ptr->root=this;
			if (other.ptr) other.ptr->root=&other;
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
	Node(K&& k, V&& v): root(nullptr), parent(nullptr), h_diff(0), x(std::move(k)), v(std::move(v)) {}

	std::unique_ptr<Node>* parent_ptr() {
		if (parent) return parent->left.get()==this ? &parent->left : &parent->right;
		else if (root) return &root->ptr;
		else return nullptr;
	}

	Node* rot_right() {
		std::unique_ptr<Node>& ptr = *parent_ptr();

		ptr.release();
		ptr.swap(right);
		right.swap(ptr->left);
		if (right) right->parent = this;
		ptr->left=std::unique_ptr<Node>(this);
		
		ptr->parent = parent;
		parent = ptr.get();

		std::swap(ptr->root, root);

		if (ptr->h_diff<=0) h_diff--;
		else h_diff -= ptr->h_diff+1;

		if (h_diff>=0) ptr->h_diff--;
		else ptr->h_diff += h_diff-1;

		return ptr.get();
	}

	Node* rot_left() {
		std::unique_ptr<Node>& ptr = *parent_ptr();

		ptr.release();
		ptr.swap(left);
		left.swap(ptr->right);
		if (left) left->parent = this;
		ptr->right=std::unique_ptr<Node>(this);

		ptr->parent = parent;
		parent = ptr.get();

		std::swap(ptr->root, root);

		if (ptr->h_diff>=0) h_diff++;
		else h_diff -= ptr->h_diff-1;

		if (h_diff<=0) ptr->h_diff++;
		else ptr->h_diff += h_diff+1;

		return ptr.get();
	}

	Node* rebalance(bool r_inc, bool rem) {
		Node* node = this;

		while (node) {
			bool is_right = node->parent && node->parent->right.get()==node;

			if (r_inc) {
				node->h_diff++;

				if (node->h_diff>=2) {
					if (node->right->h_diff==-1) {
						node->right->rot_left();
						assert(node->right->h_diff==1 || node->right->h_diff==2);
					}
					node=node->rot_right();

					if (rem && node->h_diff!=-1) r_inc=!is_right;
					else if (!rem && node->h_diff==-1) r_inc=is_right;
					else break;
				} else if (node->h_diff>0 && !rem) {
					r_inc = is_right;
				} else if (node->h_diff<=0 && rem) {
					r_inc = !is_right;
				} else {
					break;
				}
			} else {
				node->h_diff--;

				if (node->h_diff<=-2) {
					if (node->left->h_diff==1) {
						node->left->rot_right();
						assert(node->left->h_diff==-1 || node->left->h_diff==-2);
					}

					node=node->rot_left();

					if (rem && node->h_diff!=1) r_inc=!is_right;
					else if (!rem && node->h_diff==1) r_inc=is_right;
					else break;
				} else if (node->h_diff<0 && !rem) {
					r_inc = is_right;
				} else if (node->h_diff>=0 && rem) {
					r_inc = !is_right;
				} else {
					break;
				}
			}

			node=node->parent;
		}

		return node;
	}

	template<class Compare=std::less<K>>
	Node* find(K const& fx, Compare cmp={}) {
		Node* node = this;
		bool eq;

		while (true) {
			bool lt = cmp(fx,node->x);
			bool gt = cmp(node->x,fx);
			eq = !lt && !gt;

			if (node->left && lt) {
				node = node->left.get();
			} else if (gt && node->right) {
				node = node->right.get();
			} else {
				break;
			}
		}

		if (eq) {
			Node* next = node;
			next = next->left.get();

			while (next) {
				while (next && !cmp(fx,next->x) && !cmp(next->x,fx)) {
					node = next;
					next = next->left.get();
				}

				while (next && cmp(next->x,fx)) {
					next = next->right.get();
				}
			}
		}

		return node;
	}

	template<class Compare=std::less<K>>
	void insert_node(std::unique_ptr<Node>&& node, Compare cmp={}) {
		Node* res = find(node->x, cmp);

		bool lt = cmp(node->x,res->x);
		if (lt) {
			res->left = std::move(node);
			res->left->parent = res;

			res->rebalance(false, false);
		} else if (cmp(res->x,node->x) || !res->right) {
			res->right = std::move(node);
			res->right->parent = res;

			res->rebalance(true, false);
		} else {
			res->right->insert_node(std::move(node), cmp);
		}
	}

	template<class Compare=std::less<K>>
	Node* insert(K&& ix, V&& iv, Compare ins_cmp={}) {
		std::unique_ptr<Node> node = std::make_unique<Node>(std::move(ix), std::move(iv));
		Node* ret = node.get();
		insert_node(std::move(node), ins_cmp);
		return ret;
	}

	std::unique_ptr<Node> remove() {
		std::unique_ptr<Node>& ptr = *parent_ptr();

		std::unique_ptr<Node> ret;
		if (left && !right) {
			ret.swap(left);
		} else if (right && !left) {
			ret.swap(right);
		}

		if (ret) {
			ret->parent = parent;
			ret->root = root;
		}

		if (left && right) {
			std::unique_ptr<Node>* rl_ref = &right;
			while ((*rl_ref)->left) rl_ref = &(*rl_ref)->left;
			std::unique_ptr<Node>& rl = *rl_ref;

			bool left_child = rl!=right;

			//i dont know why this is so convoluted. who doesnt like swapping?!?/
			ret.swap(rl);

			Node* prev_parent = left_child ? ret->parent : ret.get();
			std::unique_ptr<Node> prev_r;

			if (ret->right) {
				rl.swap(ret->right);
				rl->parent = prev_parent;
			}

			if (right) {
				prev_r.swap(right);
				prev_r->parent = ret.get();
			}

			if (left) {
				ret->left.swap(left);
				ret->left->parent = ret.get();
			}

			ret->h_diff = h_diff;
			ret->right.swap(prev_r);

			ret->parent = ptr->parent;
			ret->root = ptr->root;
			ptr.swap(ret);

			prev_parent->rebalance(left_child, true);
		} else {
			ptr.swap(ret);

			if (parent) parent->rebalance(&parent->left==&ptr, true);
		}

		return ret;
	}

	Iterator begin() {
		Iterator iter {.current=this, .begin=this, .end=parent};
		while (iter.current->left) iter.current = iter.current->left.get();
		return iter;
	}

	Iterator end() {
		return {.current=parent, .begin=this, .end=parent};
	}

	Iterator iter_ref(Node* ref) {
		return {.current=ref, .begin=this, .end=parent};
	}

	void swap_positions(Node* other) {
		std::unique_ptr<Node>* pptr = parent_ptr();
		std::unique_ptr<Node>* opptr = other->parent_ptr();
		pptr->release();
		opptr->release();

		if (parent==other) {
			if (pptr==&other->left) pptr=&left; else pptr=&right;
		} else if (other->parent==this) {
			if (opptr==&left) opptr=&other->left; else opptr=&other->right;
		}

		std::swap(left, other->left);
		std::swap(right, other->right);
		std::swap(parent, other->parent);
		std::swap(root, other->root);
		std::swap(h_diff, other->h_diff);

		pptr->reset(other);
		opptr->reset(this);

		if (right) right->parent = this;
		if (left) left->parent = this;
		if (other->right) other->right->parent = other;
		if (other->left) other->left->parent = other;
	}

//	Node merge(Node* other) {
//		Node new_root;
//		for (Iterator iter = begin(); )
//	}
};

#endif