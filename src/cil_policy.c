#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sepol/errcodes.h>
#include "cil_tree.h"
#include "cil_list.h"
#include "cil.h"
#include "cil_mem.h"
#include "cil_policy.h"

#define SEPOL_DONE			555

#define COMMONS				0
#define CLASSES				1
#define INTERFACES			2
#define ATTRTYPES			3
#define ALIASES				4
#define ALLOWS				5
#define USERROLES			6

#define BUFFER				1024
#define NUM_POLICY_FILES 	7 

int cil_combine_policy(FILE **file_arr, FILE *policy_file)
{
	char temp[BUFFER];
	int i, rc, rc_read, rc_write;

	for(i=0; i<NUM_POLICY_FILES; i++) {
		fseek(file_arr[i], 0, SEEK_SET);
		while (!feof(file_arr[i])) {
			rc_read = fread(temp, 1, BUFFER, file_arr[i]);
			if (rc_read == 0 && ferror(file_arr[i])) {
				printf("Error reading temp policy file\n");
				return SEPOL_ERR;
			}
			rc_write = 0;
			while (rc_read > rc_write) {
				rc = fwrite(temp+rc_write, 1, rc_read-rc_write, policy_file);
				rc_write += rc;
				if (rc == 0 && ferror(file_arr[i])) {
					printf("Error writing to policy.conf\n");
					return SEPOL_ERR;
				}
			}
		}
	}

	return SEPOL_OK;
}

static int __cil_multimap_insert_key(struct cil_list_item **curr_key, struct cil_symtab_datum *key, struct cil_symtab_datum *value, uint32_t key_flavor, uint32_t val_flavor)
{
	struct cil_list_item *new_key = NULL;
	cil_list_item_init(&new_key);
	struct cil_multimap_item *new_data = cil_malloc(sizeof(struct cil_multimap_item));
	new_data->key = key;
	cil_list_init(&new_data->values);
	if (value != NULL) {
		cil_list_item_init(&new_data->values->head);
		new_data->values->head->data = value;
		new_data->values->head->flavor = val_flavor;
	}
	new_key->flavor = key_flavor;
	new_key->data = new_data;
	if (*curr_key == NULL)
		*curr_key = new_key;
	else
		(*curr_key)->next = new_key;

	return SEPOL_OK;
}

int cil_multimap_insert(struct cil_list *list, struct cil_symtab_datum *key, struct cil_symtab_datum *value, uint32_t key_flavor, uint32_t val_flavor)
{
	if (list == NULL || key == NULL) 
		return SEPOL_ERR;

	struct cil_list_item *curr_key = list->head;
	struct cil_list_item *curr_value = NULL;

	if (curr_key == NULL) 
		__cil_multimap_insert_key(&list->head, key, value, key_flavor, val_flavor);
	while(curr_key != NULL) {
		if ((struct cil_multimap_item*)curr_key->data != NULL) {
			if (((struct cil_multimap_item*)curr_key->data)->key != NULL && ((struct cil_multimap_item*)curr_key->data)->key == key) {
				curr_value = ((struct cil_multimap_item*)curr_key->data)->values->head;

				struct cil_list_item *new_value = NULL;
				cil_list_item_init(&new_value);
				new_value->data = value;
				new_value->flavor = val_flavor;

				if (curr_value == NULL) {
					((struct cil_multimap_item*)curr_key->data)->values->head = new_value;
					return SEPOL_OK;
				}
				while (curr_value != NULL) {
					if (curr_value == (struct cil_list_item*)value) {
						free(new_value);
						break;
					}
					if (curr_value->next == NULL) {
						curr_value->next = new_value;
						return SEPOL_OK;
					}
					curr_value = curr_value->next;
				}
			}	
			else if (curr_key->next == NULL) {
				__cil_multimap_insert_key(&curr_key, key, value, key_flavor, val_flavor);
				return SEPOL_OK;
			}
		}
		else {
			printf("No data in list item\n");
			return SEPOL_ERR;
		}
		curr_key = curr_key->next;
	}
	return SEPOL_OK;
}

int cil_userrole_to_policy(FILE **file_arr, struct cil_list *userroles)
{
	if (userroles == NULL) 
		return SEPOL_OK;
	
	struct cil_list_item *current_user = userroles->head;
	while (current_user != NULL) {
		if (((struct cil_multimap_item*)current_user->data)->values->head == NULL) {
			printf("No roles associated with user %s (line %d)\n",  ((struct cil_multimap_item*)current_user->data)->key->name,  ((struct cil_multimap_item*)current_user->data)->key->node->line);
			return SEPOL_ERR;
		}
		fprintf(file_arr[USERROLES], "user %s roles {", ((struct cil_multimap_item*)current_user->data)->key->name);
		struct cil_list_item *current_role = ((struct cil_multimap_item*)current_user->data)->values->head;
		while (current_role != NULL) {
			fprintf(file_arr[USERROLES], " %s",  ((struct cil_role*)current_role->data)->datum.name);
			current_role = current_role->next;
		}
		fprintf(file_arr[USERROLES], " };\n"); 
		current_user = current_user->next;
	}
	return SEPOL_OK;
}

int cil_cat_to_policy(FILE **file_arr, struct cil_list *cats)
{
	if (cats == NULL) 
		return SEPOL_OK;
	
	struct cil_list_item *curr_cat = cats->head;
	while (curr_cat != NULL) {
		if (((struct cil_multimap_item*)curr_cat->data)->values->head == NULL) 
			fprintf(file_arr[USERROLES], "category %s;\n", ((struct cil_multimap_item*)curr_cat->data)->key->name);
		else {
			fprintf(file_arr[USERROLES], "category %s alias", ((struct cil_multimap_item*)curr_cat->data)->key->name);
			struct cil_list_item *curr_catalias = ((struct cil_multimap_item*)curr_cat->data)->values->head;
			while (curr_catalias != NULL) {
				fprintf(file_arr[USERROLES], " %s",  ((struct cil_cat*)curr_catalias->data)->datum.name);
				curr_catalias = curr_catalias->next;
			}
			fprintf(file_arr[USERROLES], ";\n"); 
		}
		curr_cat = curr_cat->next;
	}
	return SEPOL_OK;
}

int cil_sens_to_policy(FILE **file_arr, struct cil_list *sens)
{
	if (sens == NULL) 
		return SEPOL_OK;
	
	struct cil_list_item *curr_sens = sens->head;
	while (curr_sens != NULL) {
		if (((struct cil_multimap_item*)curr_sens->data)->values->head == NULL) 
			fprintf(file_arr[USERROLES], "sensitivity %s;\n", ((struct cil_multimap_item*)curr_sens->data)->key->name);
		else {
			fprintf(file_arr[USERROLES], "sensitivity %s alias", ((struct cil_multimap_item*)curr_sens->data)->key->name);
			struct cil_list_item *curr_sensalias = ((struct cil_multimap_item*)curr_sens->data)->values->head;
			while (curr_sensalias != NULL) {
				fprintf(file_arr[USERROLES], " %s",  ((struct cil_sens*)curr_sensalias->data)->datum.name);
				curr_sensalias = curr_sensalias->next;
			}
			fprintf(file_arr[USERROLES], ";\n"); 
		}
		curr_sens = curr_sens->next;
	}
	return SEPOL_OK;
}

int cil_name_to_policy(FILE **file_arr, struct cil_tree_node *current) 
{
	char *name = ((struct cil_symtab_datum*)current->data)->name;
	uint32_t flavor = current->flavor;

	switch(flavor) {
		case CIL_ATTR: {
			fprintf(file_arr[ATTRTYPES], "attribute %s;\n", name);
			break;
		}
		case CIL_TYPE: {
			fprintf(file_arr[ATTRTYPES], "type %s;\n", name);
			break;
		}
		case CIL_TYPE_ATTR: {
			struct cil_typeattribute *typeattr = (struct cil_typeattribute*)current->data;
			char *type_str = ((struct cil_symtab_datum*)typeattr->type)->name;
			char *attr_str = ((struct cil_symtab_datum*)typeattr->attr)->name;
			fprintf(file_arr[ALLOWS], "typeattribute %s %s;\n", type_str, attr_str);
			break;
		}
		case CIL_TYPEALIAS: {
			struct cil_typealias *alias = (struct cil_typealias*)current->data;
			fprintf(file_arr[ALIASES], "typealias %s alias %s;\n", ((struct cil_symtab_datum*)alias->type)->name, name);
			break;
		}
		case CIL_ROLE: {
			fprintf(file_arr[ATTRTYPES], "role %s;\n", name);
			break;
		}
		case CIL_BOOL: {
			char *boolean = ((struct cil_bool*)current->data)->value ? "true" : "false";
			fprintf(file_arr[ATTRTYPES], "bool %s %s;\n", name, boolean);
			break;
		}
		case CIL_COMMON: {
			if (current->cl_head != NULL) {
				current = current->cl_head;
				fprintf(file_arr[COMMONS], "common %s { ", name);
			}
			else {
				printf("No permissions given\n");
				return SEPOL_ERR;
			}

			while (current != NULL) {	
				if (current->flavor == CIL_PERM)
					fprintf(file_arr[COMMONS], "%s ", ((struct cil_symtab_datum*)current->data)->name);
				else {
					printf("Improper data type found in common permissions: %d\n", current->flavor);
					return SEPOL_ERR;
				}
				current = current->next;
			}
			fprintf(file_arr[COMMONS], "};\n");
			return SEPOL_DONE;
		}
		case CIL_CLASS: {
			if (current->cl_head != NULL) {
				fprintf(file_arr[CLASSES], "class %s ", ((struct cil_class*)(current->data))->datum.name);
			}
			else if (((struct cil_class*)current->data)->common == NULL) {
				printf("No permissions given\n");
				return SEPOL_ERR;
			}

			if (((struct cil_class*)current->data)->common != NULL) 
				fprintf(file_arr[CLASSES], "inherits %s ", ((struct cil_class*)current->data)->common->datum.name);

			fprintf(file_arr[CLASSES], "{ ");

			if (current->cl_head != NULL) 
				current = current->cl_head;

			while (current != NULL) {
				if (current->flavor == CIL_PERM)
					fprintf(file_arr[CLASSES], "%s ", ((struct cil_symtab_datum*)current->data)->name);
				else {
					printf("Improper data type found in class permissions: %d\n", current->flavor);
					return SEPOL_ERR;
				}
				current = current->next;
			}
			fprintf(file_arr[CLASSES], "};\n");
			return SEPOL_DONE;
		}
		case CIL_AVRULE: {
			struct cil_avrule *rule = (struct cil_avrule*)current->data;
			char *src_str = ((struct cil_symtab_datum*)(struct cil_type*)rule->src)->name;
			char *tgt_str = ((struct cil_symtab_datum*)(struct cil_type*)rule->tgt)->name;
			char *obj_str = ((struct cil_symtab_datum*)(struct cil_type*)rule->obj)->name;
			struct cil_list_item *perm_item = rule->perms_list->head;
			switch (rule->rule_kind) {
				case CIL_AVRULE_ALLOWED:
					fprintf(file_arr[ALLOWS], "allow %s %s:%s { ", src_str, tgt_str, obj_str);
					break;
				case CIL_AVRULE_AUDITALLOW:
					fprintf(file_arr[ALLOWS], "auditallow %s %s:%s { ", src_str, tgt_str, obj_str);
					break;
				case CIL_AVRULE_DONTAUDIT:
					fprintf(file_arr[ALLOWS], "dontaudit %s %s:%s { ", src_str, tgt_str, obj_str);
					break;
				case CIL_AVRULE_NEVERALLOW:
					fprintf(file_arr[ALLOWS], "neverallow %s %s:%s { ", src_str, tgt_str, obj_str);
					break;
				default : {
					printf("Unknown avrule kind: %d\n", rule->rule_kind);
					return SEPOL_ERR;
				}
			}
			while (perm_item != NULL) {
				fprintf(file_arr[ALLOWS], "%s ", ((struct cil_perm*)(perm_item->data))->datum.name);
				perm_item = perm_item->next;
			}
			fprintf(file_arr[ALLOWS], "};\n");
			break;
		}
		case CIL_TYPE_RULE: {
			struct cil_type_rule *rule = (struct cil_type_rule*)current->data;
			char *src_str = ((struct cil_symtab_datum*)(struct cil_type*)rule->src)->name;
			char *tgt_str = ((struct cil_symtab_datum*)(struct cil_type*)rule->tgt)->name;
			char *obj_str = ((struct cil_symtab_datum*)(struct cil_class*)rule->obj)->name;
			char *result_str = ((struct cil_symtab_datum*)(struct cil_type*)rule->result)->name;
			
			switch (rule->rule_kind) {
				case CIL_TYPE_TRANSITION:
					fprintf(file_arr[ALLOWS], "type_transition %s %s : %s %s;\n", src_str, tgt_str, obj_str, result_str);
					break;
				case CIL_TYPE_CHANGE:
					fprintf(file_arr[ALLOWS], "type_change %s %s : %s %s\n;", src_str, tgt_str, obj_str, result_str);
					break;
				case CIL_TYPE_MEMBER:
					fprintf(file_arr[ALLOWS], "type_member %s %s : %s %s;\n", src_str, tgt_str, obj_str, result_str);
					break;
				default : {
					printf("Unknown type_rule kind: %d\n", rule->rule_kind);
					return SEPOL_ERR;
				}
			}
			break;
		}
		case CIL_ROLETRANS: {
			struct cil_role_trans *roletrans = (struct cil_role_trans*)current->data;
			char *src_str = ((struct cil_symtab_datum*)(struct cil_role*)roletrans->src)->name;
			char *tgt_str = ((struct cil_symtab_datum*)(struct cil_type*)roletrans->tgt)->name;
			char *result_str = ((struct cil_symtab_datum*)(struct cil_role*)roletrans->result)->name;
			
			fprintf(file_arr[ALLOWS], "role_transition %s %s %s;\n", src_str, tgt_str, result_str);
			break;
		}
		case CIL_ROLEALLOW: {
			struct cil_role_allow *roleallow = (struct cil_role_allow*)current->data;
			char *src_str = ((struct cil_symtab_datum*)(struct cil_role*)roleallow->src)->name;
			char *tgt_str = ((struct cil_symtab_datum*)(struct cil_type*)roleallow->tgt)->name;
			
			fprintf(file_arr[ALLOWS], "roleallow %s %s;\n", src_str, tgt_str);
			break;
		}
		case CIL_ROLETYPE : {
			struct cil_roletype *roletype = (struct cil_roletype*)current->data;
			char *role_str = ((struct cil_symtab_datum*)(struct cil_role*)roletype->role)->name;
			char *type_str = ((struct cil_symtab_datum*)(struct cil_type*)roletype->type)->name;
		
			fprintf(file_arr[ALIASES], "role %s types %s\n", role_str, type_str);
			break;
		}
		default : {
			break;
		}
	}

	return SEPOL_OK;
}

int cil_gen_policy(struct cil_tree_node *root)
{
	struct cil_tree_node *curr = root;
	int rc = SEPOL_ERR;
	int reverse = 0;
	FILE *policy_file;
	FILE **file_arr = cil_malloc(sizeof(FILE*) * NUM_POLICY_FILES);
	char *file_path_arr[NUM_POLICY_FILES];
	char temp[32];

	struct cil_list *users;
	cil_list_init(&users);
	struct cil_list *cats;
	cil_list_init(&cats);
	struct cil_list *sens;
	cil_list_init(&sens);

	strcpy(temp,"/tmp/common-XXXXXX");
	file_arr[COMMONS] = fdopen(mkstemp(temp), "w+");
	file_path_arr[COMMONS] = cil_strdup(temp);
	
	strcpy(temp, "/tmp/class-XXXXXX");
	file_arr[CLASSES] = fdopen(mkstemp(temp), "w+");
	file_path_arr[CLASSES] = cil_strdup(temp);

	strcpy(temp, "/tmp/interf-XXXXXX");
	file_arr[INTERFACES] = fdopen(mkstemp(temp), "w+");
	file_path_arr[INTERFACES] = cil_strdup(temp);
	
	strcpy(temp, "/tmp/attrtypes-XXXXXX");
	file_arr[ATTRTYPES] = fdopen(mkstemp(temp), "w+");
	file_path_arr[ATTRTYPES] = cil_strdup(temp);
	
	strcpy(temp, "/tmp/aliases-XXXXXX");
	file_arr[ALIASES] = fdopen(mkstemp(temp), "w+");
	file_path_arr[ALIASES] = cil_strdup(temp);
	
	strcpy(temp, "/tmp/allows-XXXXXX");
	file_arr[ALLOWS] = fdopen(mkstemp(temp), "w+");
	file_path_arr[ALLOWS] = cil_strdup(temp);
	
	strcpy(temp, "/tmp/userroles-XXXXXX");
	file_arr[USERROLES] = fdopen(mkstemp(temp), "w+");
	file_path_arr[USERROLES] = cil_strdup(temp);

	policy_file = fopen("policy.conf", "w+");	

	do {
		if (curr->cl_head != NULL) {
			if (!reverse) {
				if (curr->flavor != CIL_ROOT) {
					rc = cil_name_to_policy(file_arr, curr);
					if (rc != SEPOL_OK && rc != SEPOL_DONE) {
						printf("Error converting node to policy %d\n", curr->flavor);
						return SEPOL_ERR;
					}
				}
			}
		}
		else {
			switch (curr->flavor) {
				case CIL_USER: {
					cil_multimap_insert(users, curr->data, NULL, CIL_USERROLE, 0);
					break;
				}
				case CIL_USERROLE: {
					cil_multimap_insert(users, &((struct cil_userrole*)curr->data)->user->datum, &((struct cil_userrole*)curr->data)->role->datum, CIL_USERROLE, CIL_ROLE);
					break;
				}
				case CIL_CATALIAS: {
					cil_multimap_insert(cats, &((struct cil_catalias*)curr->data)->cat->datum, curr->data, CIL_CAT, CIL_CATALIAS);
					break;
				}
				case CIL_SENS: {
					cil_multimap_insert(sens, curr->data, NULL, CIL_SENS, 0);
					break;
				}
				case CIL_SENSALIAS: {
					cil_multimap_insert(sens, &((struct cil_sensalias*)curr->data)->sens->datum, curr->data, CIL_SENS, CIL_SENSALIAS);
					break;
				}
				default : {
					rc = cil_name_to_policy(file_arr, curr);
					if (rc != SEPOL_OK && rc != SEPOL_DONE) {
						printf("Error converting node to policy %d\n", rc);
						return SEPOL_ERR;
					}
					break;
				}
			}
		}
	
		if (curr->cl_head != NULL && !reverse && rc != SEPOL_DONE)
			curr = curr->cl_head;
		else if (curr->next != NULL) {
			curr = curr->next;
			reverse = 0;
		}
		else {
			curr = curr->parent;
			reverse = 1;
		}
	} while (curr->flavor != CIL_ROOT);

	rc = cil_userrole_to_policy(file_arr, users);
	if (rc != SEPOL_OK) {
		printf("Error creating policy.conf\n");
		return SEPOL_ERR;
	}

	rc = cil_sens_to_policy(file_arr, sens);
	if (rc != SEPOL_OK) {
		printf("Error creating policy.conf\n");
		return SEPOL_ERR;
	}


	rc = cil_cat_to_policy(file_arr, cats);
	if (rc != SEPOL_OK) {
		printf("Error creating policy.conf\n");
		return SEPOL_ERR;
	}

	rc = cil_combine_policy(file_arr, policy_file);
	if (rc != SEPOL_OK) {
		printf("Error creating policy.conf\n");
		return SEPOL_ERR;
	}

	// Remove temp files
	int i;
	for (i=0; i<NUM_POLICY_FILES; i++) {
		rc = fclose(file_arr[i]);
		if (rc != 0) {
			printf("Error closing temporary file\n");
			return SEPOL_ERR;
		}
		rc = unlink(file_path_arr[i]);
		if (rc != 0) {
			printf("Error unlinking temporary files\n");
			return SEPOL_ERR;
		}
		free(file_path_arr[i]);
	}

	rc = fclose(policy_file);
	if (rc != 0) {
		printf("Error closing policy.conf\n");
		return SEPOL_ERR;
	}
	free(file_arr);

	return SEPOL_OK;
}
